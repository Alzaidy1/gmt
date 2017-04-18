/*--------------------------------------------------------------------
 *    $Id$
 *
 *	Copyright (c) 1991-2017 by P. Wessel, W. H. F. Smith, R. Scharroo, J. Luis and F. Wobbe
 *	See LICENSE.TXT file for copying and redistribution conditions.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU Lesser General Public License as published by
 *	the Free Software Foundation; version 3 or any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU Lesser General Public License for more details.
 *
 *	Contact info: gmt.soest.hawaii.edu
 *--------------------------------------------------------------------*/
/*
 * API functions to support the blockmedian application.
 *
 * Author:	Walter H.F. Smith
 * Date:	1-JAN-2010
 * Version:	5 API
 *
 * Brief synopsis: reads records of x, y, data, [weight] and writes out median
 * value per cell, where cellular region is bounded by West East South North
 * and cell dimensions are delta_x, delta_y.
 */

#define BLOCKMEDIAN	/* Since mean, median, mode share near-similar macros we require this setting */

#define THIS_MODULE_NAME	"blockmedian"
#define THIS_MODULE_LIB		"core"
#define THIS_MODULE_PURPOSE	"Block average (x,y,z) data tables by L1 norm (spatial median)"
#define THIS_MODULE_KEYS	"<D{,>D}"

#include "gmt_dev.h"
#include "block_subs.h"

#define GMT_PROG_OPTIONS "-:>RVabdfghior" GMT_OPT("FH")

GMT_LOCAL int usage (struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose (API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return (GMT_NOERROR);
	GMT_Message (API, GMT_TIME_NONE, "usage: blockmedian [<table>] %s\n", GMT_I_OPT);
	GMT_Message (API, GMT_TIME_NONE, "\t%s [-C] [-E[b]] [-Er|s[-]] [-Q] [-T<q>] [%s]\n\t[-W[i][o][+s]] [%s] [%s] [%s] [%s]\n\t[%s] [%s]\n\t[%s] [%s] [%s]\n\n",
		GMT_Rgeo_OPT, GMT_V_OPT, GMT_a_OPT, GMT_b_OPT, GMT_d_OPT, GMT_f_OPT, GMT_h_OPT, GMT_i_OPT, GMT_o_OPT, GMT_r_OPT, GMT_colon_OPT);

	if (level == GMT_SYNOPSIS) return (GMT_MODULE_SYNOPSIS);

	GMT_Option (API, "I,R");
	GMT_Message (API, GMT_TIME_NONE, "\n\tOPTIONS:\n");
	GMT_Option (API, "<");
	GMT_Message (API, GMT_TIME_NONE, "\t-C Output center of block as location [Default is (median x, median y), but see -Q].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-E Extend output with L1 scale (s), low (l), and high (h) value per block, i.e.,\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   output (x,y,z,s,l,h[,w]) [Default outputs (x,y,z[,w])]; see -W regarding w.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   Use -Eb for box-and-whisker output (x,y,z,l,25%%q,75%%q,h[,w]).\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   Use -Er to report record number of the median value per block,\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   or -Es to report an unsigned integer source id (sid) taken from the x,y,z[,w],sid input.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   For ties, report record number (or sid) of largest value; append - for smallest.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-Q Quicker; get median z and x,y at that z [Default gets median x, median y, median z].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-T Set quantile (0 < q < 1) to report [Default is 0.5 which is the median of z].\n");
	GMT_Option (API, "V");
	GMT_Message (API, GMT_TIME_NONE, "\t-W Set Weight options.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t     -Wi reads Weighted Input (4 cols: x,y,z,w) but skips w on output.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t     -Wo reads unWeighted Input (3 cols: x,y,z) but weight sum on output.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t     -W with no modifier has both weighted Input and Output; Default is no weights used.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t     Append +s read/write standard deviations instead, with w = 1/s.\n");
	GMT_Option (API, "a,bi");
	if (gmt_M_showusage (API)) GMT_Message (API, GMT_TIME_NONE, "\t   Default is 3 columns (or 4 if -W is set).\n");
	GMT_Option (API, "bo,d,f,h,i,o,r,:,.");

	return (GMT_MODULE_USAGE);
}

GMT_LOCAL int parse (struct GMT_CTRL *GMT, struct BLOCKMEDIAN_CTRL *Ctrl, struct GMT_OPTION *options) {
	/* This parses the options provided to blockmedian and sets parameters in CTRL.
	 * Any GMT common options will override values set previously by other commands.
	 * It also replaces any file names specified as input or output with the data ID
	 * returned when registering these sources/destinations with the API.
	 */

	unsigned int n_errors = 0;
	bool sigma;
	char arg[GMT_LEN16] = {""};
	struct GMT_OPTION *opt = NULL;

	for (opt = options; opt; opt = opt->next) {
		switch (opt->option) {

			case '<':	/* Skip input files */
				if (!gmt_check_filearg (GMT, '<', opt->arg, GMT_IN, GMT_IS_DATASET)) n_errors++;
				break;

			/* Processes program-specific parameters */

			case 'C':	/* Report center of block instead */
				Ctrl->C.active = true;
				break;
			case 'E':	/* Report extended statistics */
				Ctrl->E.active = true;			/* Report standard deviation, min, and max in cols 4-6 */
				if (opt->arg[0] == 'b')
					Ctrl->E.mode = BLK_DO_EXTEND4;		/* Report min, 25%, 75% and max in cols 4-7 */
				else if (opt->arg[0] == 'r' || opt->arg[0] == 's') {
					Ctrl->E.mode = (opt->arg[1] == '-') ? BLK_DO_INDEX_LO : BLK_DO_INDEX_HI;	/* Report row number or sid of median */
					if (opt->arg[0] == 's') /* report sid */
						Ctrl->E.mode |= BLK_DO_SRC_ID;
				}
				else if (opt->arg[0] == '\0')
					Ctrl->E.mode = BLK_DO_EXTEND3;		/* Report L1scale, low, high in cols 4-6 */
				else
					n_errors++;
				break;
			case 'I':	/* Get block dimensions */
				Ctrl->I.active = true;
				if (gmt_getinc (GMT, opt->arg, Ctrl->I.inc)) {
					gmt_inc_syntax (GMT, 'I', 1);
					n_errors++;
				}
				break;
			case 'Q':	/* Quick mode for median z */
				Ctrl->Q.active = true;		/* Get median z and (x,y) of that point */
				break;
			case 'T':	/* Select a particular quantile [0.5 (median)] */
				Ctrl->T.active = true;
				Ctrl->T.quantile = atof (opt->arg);
				break;
			case 'W':	/* Use in|out weights */
				Ctrl->W.active = true;
				sigma = (gmt_get_modifier (opt->arg, 's', arg)) ? true : false;
				switch (arg[0]) {
					case '\0':
						Ctrl->W.weighted[GMT_IN] = Ctrl->W.weighted[GMT_OUT] = true;
						Ctrl->W.sigma[GMT_IN] = Ctrl->W.sigma[GMT_OUT] = sigma;
						break;
					case 'i': case 'I':
						Ctrl->W.weighted[GMT_IN] = true;
						Ctrl->W.sigma[GMT_IN] = sigma;
						break;
					case 'o': case 'O':
						Ctrl->W.weighted[GMT_OUT] = true;
						Ctrl->W.sigma[GMT_OUT] = sigma;
						break;
					default:
						n_errors++;
						break;
				}
				break;

			default:	/* Report bad options */
				n_errors += gmt_default_error (GMT, opt->option);
				break;
		}
	}

	gmt_check_lattice (GMT, Ctrl->I.inc, &GMT->common.r.registration, &Ctrl->I.active);	/* If -R<grdfile> was given we may get incs unless -I was used */

	n_errors += gmt_M_check_condition (GMT, !GMT->common.R.active, "Syntax error: Must specify -R option\n");
	n_errors += gmt_M_check_condition (GMT, Ctrl->T.quantile <= 0.0 || Ctrl->T.quantile >= 1.0,
			"Syntax error: 0 < q < 1 for quantile in -T [0.5]\n");
	n_errors += gmt_M_check_condition (GMT, Ctrl->I.inc[GMT_X] <= 0.0 || Ctrl->I.inc[GMT_Y] <= 0.0,
			"Syntax error -I option: Must specify positive increment(s)\n");
	n_errors += gmt_check_binary_io (GMT, (Ctrl->W.weighted[GMT_IN]) ? 4 : 3);

	return (n_errors ? GMT_PARSE_ERROR : GMT_NOERROR);
}

GMT_LOCAL void median_output (struct GMT_CTRL *GMT, struct GMT_GRID_HEADER *h, uint64_t first_in_cell, uint64_t first_in_new_cell, double weight_sum, double *out, double *extra, unsigned int go_quickly, unsigned int emode, double *quantile, unsigned int n_quantiles, struct BLK_DATA *data) {
	double weight_half, weight_count;
	uint64_t node, n_in_cell, node1;
	unsigned int k, k_for_xy;
	int way;
	gmt_M_unused(GMT);

	/* Remember: Data are already sorted on z for each cell */

	/* Step 1: Find the n_quantiles requested (typically only one unless -Eb was used) */

	n_in_cell = first_in_new_cell - first_in_cell;
	node = first_in_cell;
	weight_count = data[first_in_cell].a[BLK_W];
	k_for_xy = (n_quantiles == 3) ? 1 : 0;	/* If -Eb is set get get median location, else same as for z (unless -Q) */
	for (k = 0; k < n_quantiles; k++) {

		weight_half = quantile[k] * weight_sum;	/* Normally, quantile will be 0.5 (i.e., median), hence the name of the variable */

		/* Determine the point where we hit the desired quantile */

		while (weight_count < weight_half) weight_count += data[++node].a[BLK_W];	/* Wind up until weight_count hits the mark */

		if (weight_count == weight_half) {
			node1 = node + 1;
			extra[k] = 0.5 * (data[node].a[BLK_Z] + data[node1].a[BLK_Z]);
			if (k == k_for_xy && go_quickly == 1) {	/* Only get x,y at the z-quantile if requested [-Q] */
				out[GMT_X] = 0.5 * (data[node].a[GMT_X] + data[node1].a[GMT_X]);
				out[GMT_Y] = 0.5 * (data[node].a[GMT_Y] + data[node1].a[GMT_Y]);
			}
			if (k == 0 && emode) {	/* Return record number of median, selecting the high or the low value since it is a tie */
				way = (data[node].a[BLK_Z] >= data[node1].a[BLK_Z]) ? +1 : -1;
				if (emode & BLK_DO_INDEX_HI) extra[3] = (way == +1) ? (double)data[node].src_id : (double)data[node1].src_id;
				else extra[3] = (way == +1) ? (double)data[node1].src_id : (double)data[node].src_id;
			}
		}
		else {
			extra[k] = data[node].a[BLK_Z];
			if (k == k_for_xy && go_quickly == 1) {	/* Only get x,y at the z-quantile if requested [-Q] */
				out[GMT_X] = data[node].a[GMT_X];
				out[GMT_Y] = data[node].a[GMT_Y];
			}
			if (k == 0 && emode) extra[3] = (double)data[node].src_id;	/* Return record number of median */
		}
	}
	out[GMT_Z] = extra[k_for_xy];	/* The desired quantile is passed via z */

	if (go_quickly == 1) return;	/* Already have everything requested so we return */

	if (go_quickly == 2) {	/* Return center of block instead of computing a representative location */
		uint64_t row, col;
		row = gmt_M_row (h, data[node].ij);
		col = gmt_M_col (h, data[node].ij);
		out[GMT_X] = gmt_M_grd_col_to_x (GMT, col, h);
		out[GMT_Y] = gmt_M_grd_row_to_y (GMT, row, h);
		return;
	}

	/* We get here when we need separate quantile calculations for both x and y locations */

	weight_half = quantile[k_for_xy] * weight_sum;	/* We want the same quantile for locations as was used for z */

	if (n_in_cell > 2) qsort(&data[first_in_cell], n_in_cell, sizeof (struct BLK_DATA), BLK_compare_x);
	node = first_in_cell;
	weight_count = data[first_in_cell].a[BLK_W];
	while (weight_count < weight_half) weight_count += data[++node].a[BLK_W];
	out[GMT_X] = (weight_count == weight_half) ?  0.5 * (data[node].a[GMT_X] + data[node + 1].a[GMT_X]) : data[node].a[GMT_X];

	if (n_in_cell > 2) qsort (&data[first_in_cell], n_in_cell, sizeof (struct BLK_DATA), BLK_compare_y);
	node = first_in_cell;
	weight_count = data[first_in_cell].a[BLK_W];
	while (weight_count < weight_half) weight_count += data[++node].a[BLK_W];
	out[GMT_Y] = (weight_count == weight_half) ? 0.5 * (data[node].a[GMT_Y] + data[node + 1].a[GMT_Y]) : data[node].a[GMT_Y];
}

/* Must free allocated memory before returning */
#define bailout(code) {gmt_M_free_options (mode); return (code);}
#define Return(code) {GMT_Destroy_Data (API, &Grid); Free_Ctrl (GMT, Ctrl); gmt_end_module (GMT, GMT_cpy); bailout (code);}

int GMT_blockmedian (void *V_API, int mode, void *args) {
	uint64_t n_lost, node, first_in_cell, first_in_new_cell;
	uint64_t n_read, nz, n_pitched, n_cells_filled, w_col, i_col = 0, sid_col;

	size_t n_alloc = 0, nz_alloc = 0;

	bool do_extra = false, duplicate_col;

	int error = 0;
	unsigned int row, col, emode = 0, n_input, n_output, n_quantiles = 1, go_quickly = 0;

	double out[8], wesn[4], quantile[3] = {0.25, 0.5, 0.75}, extra[8], weight, half_dx, *in = NULL, *z_tmp = NULL;

	char format[GMT_LEN256] = {""}, *old_format = NULL;

	struct GMT_OPTION *options = NULL;
	struct GMT_GRID *Grid = NULL;
	struct BLK_DATA *data = NULL;
	struct BLOCKMEDIAN_CTRL *Ctrl = NULL;
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMTAPI_CTRL *API = gmt_get_api_ptr (V_API);	/* Cast from void to GMTAPI_CTRL pointer */

	/*----------------------- Standard module initialization and parsing ----------------------*/

	if (API == NULL) return (GMT_NOT_A_SESSION);
	if (mode == GMT_MODULE_PURPOSE) return (usage (API, GMT_MODULE_PURPOSE));	/* Return the purpose of program */
	options = GMT_Create_Options (API, mode, args);	if (API->error) return (API->error);	/* Set or get option list */

	if (!options || options->option == GMT_OPT_USAGE) bailout (usage (API, GMT_USAGE));	/* Return the usage message */
	if (options->option == GMT_OPT_SYNOPSIS) bailout (usage (API, GMT_SYNOPSIS));	/* Return the synopsis */

	/* Parse the command-line arguments */

	GMT = gmt_begin_module (API, THIS_MODULE_LIB, THIS_MODULE_NAME, &GMT_cpy); /* Save current state */
	if (GMT_Parse_Common (API, GMT_PROG_OPTIONS, options)) Return (API->error);
	Ctrl = New_Ctrl (GMT);	/* Allocate and initialize a new control structure */
	if ((error = parse (GMT, Ctrl, options)) != 0) Return (error);

	/*---------------------------- This is the blockmedian main code ----------------------------*/

	GMT_Report (API, GMT_MSG_VERBOSE, "Processing input table data\n");

	if ((Grid = GMT_Create_Data (API, GMT_IS_GRID, GMT_IS_SURFACE, GMT_GRID_HEADER_ONLY, NULL, NULL, Ctrl->I.inc, \
		GMT_GRID_DEFAULT_REG, 0, NULL)) == NULL) Return (API->error);	/* Note: 0 for pad since no BC work needed */

	duplicate_col = (gmt_M_360_range (Grid->header->wesn[XLO], Grid->header->wesn[XHI]) && Grid->header->registration == GMT_GRID_NODE_REG);	/* E.g., lon = 0 column should match lon = 360 column */
	half_dx = 0.5 * Grid->header->inc[GMT_X];
	go_quickly = (Ctrl->Q.active) ? 1 : 0;
	if (Ctrl->C.active && go_quickly == 1) {
		GMT_Report (API, GMT_MSG_NORMAL, "Warning: -C overrides -Q\n");
		go_quickly = 0;
	}
	if (Ctrl->C.active) go_quickly = 2;			/* Flag used in output calculation */
	n_input = 3 + Ctrl->W.weighted[GMT_IN] + ((Ctrl->E.mode & BLK_DO_SRC_ID) ? 1 : 0);	/* 3 columns on output, plus 1 extra if -W and another if -Es  */
	n_output = (Ctrl->W.weighted[GMT_OUT]) ? 4 : 3;		/* 3 columns on output, plus 1 extra if -W  */
	if (Ctrl->E.active) {					/* One or more -E settings */
		if (Ctrl->E.mode & BLK_DO_EXTEND3) {	/* Add s,l,h cols */
			n_output += 3;
			do_extra = true;
		}
		else if (Ctrl->E.mode & BLK_DO_EXTEND4) {	/* Add l, 25%, 75%, h cols */
			n_output += 4;
			n_quantiles = 3;
			do_extra = true;
		}
		if (Ctrl->E.mode & BLK_DO_INDEX_LO || Ctrl->E.mode & BLK_DO_INDEX_HI) {	/* Add index */
			n_output++;
			emode = Ctrl->E.mode & (BLK_DO_INDEX_LO + BLK_DO_INDEX_HI);
		}
	}
	if (!(Ctrl->E.mode & BLK_DO_EXTEND4)) quantile[0] = Ctrl->T.quantile;	/* Just get the single quantile [median] */

	if (gmt_M_is_verbose (GMT, GMT_MSG_VERBOSE)) {
		snprintf (format, GMT_LEN256, "W: %s E: %s S: %s N: %s n_columns: %%d n_rows: %%d\n", GMT->current.setting.format_float_out, GMT->current.setting.format_float_out, GMT->current.setting.format_float_out, GMT->current.setting.format_float_out);
		GMT_Report (API, GMT_MSG_VERBOSE, format, Grid->header->wesn[XLO], Grid->header->wesn[XHI], Grid->header->wesn[YLO], Grid->header->wesn[YHI], Grid->header->n_columns, Grid->header->n_rows);
	}

	gmt_set_xy_domain (GMT, wesn, Grid->header);	/* May include some padding if gridline-registered */

	/* Specify input and output expected columns */
	if ((error = gmt_set_cols (GMT, GMT_IN, n_input)) != GMT_NOERROR) {
		Return (error);
	}
	if ((error = gmt_set_cols (GMT, GMT_OUT, n_output)) != GMT_NOERROR) {
		Return (error);
	}

	/* Register likely data sources unless the caller has already done so */
	if (GMT_Init_IO (API, GMT_IS_DATASET, GMT_IS_POINT, GMT_IN,  GMT_ADD_DEFAULT, 0, options) != GMT_NOERROR) {	/* Registers default input sources, unless already set */
		Return (API->error);
	}
	if (GMT_Init_IO (API, GMT_IS_DATASET, GMT_IS_POINT, GMT_OUT, GMT_ADD_DEFAULT, 0, options) != GMT_NOERROR) {	/* Registers default output destination, unless already set */
		Return (API->error);
	}

	/* Initialize the i/o for doing record-by-record reading/writing */
	if (GMT_Begin_IO (API, GMT_IS_DATASET, GMT_IN, GMT_HEADER_ON) != GMT_NOERROR) {	/* Enables data input and sets access mode */
		Return (API->error);
	}

	sid_col = (Ctrl->W.weighted[GMT_IN]) ? 4 : 3;	/* Column with integer source id [if -Es is set] */
	n_read = n_pitched = 0;	/* Initialize counters */

	GMT->session.min_meminc = GMT_INITIAL_MEM_ROW_ALLOC;	/* Start by allocating a 32 MB chunk */

	/* Read the input data */

	do {	/* Keep returning records until we reach EOF */
		if ((in = GMT_Get_Record (API, GMT_READ_DATA, NULL)) == NULL) {	/* Read next record, get NULL if special case */
			if (gmt_M_rec_is_error (GMT)) 		/* Bail if there are any read errors */
				Return (GMT_RUNTIME_ERROR);
			if (gmt_M_rec_is_eof (GMT)) 		/* Reached end of file */
				break;
			continue;							/* Go back and read the next record */
		}

		if (gmt_M_is_dnan (in[GMT_Z])) 		/* Skip if z = NaN */
			continue;

		/* Clean data record to process */

		n_read++;						/* Number of records read */

		if (gmt_M_y_is_outside (GMT, in[GMT_Y], wesn[YLO], wesn[YHI])) continue;		/* Outside y-range */
		if (gmt_x_is_outside (GMT, &in[GMT_X], wesn[XLO], wesn[XHI])) continue;		/* Outside x-range (or longitude) */

		/* We appear to be inside: Get row and col indices of this block */

		if (gmt_row_col_out_of_bounds (GMT, in, Grid->header, &row, &col)) continue;	/* Sorry, outside after all */
		if (duplicate_col && (wesn[XHI]-in[GMT_X] < half_dx)) {	/* Only compute median values for the west column and not the repeating east column with lon += 360 */
			in[GMT_X] -= 360.0;	/* Make this point be considered for the western block mean value */
			col = 0;
		}

		/* OK, this point is definitively inside and will be used */

		node = gmt_M_ijp (Grid->header, row, col);	/* Bin node */

		if (n_pitched == n_alloc) data = gmt_M_malloc (GMT, data, n_pitched, &n_alloc, struct BLK_DATA);
		data[n_pitched].ij = node;
		data[n_pitched].src_id = (Ctrl->E.mode & BLK_DO_SRC_ID) ? (uint64_t)lrint (in[sid_col]) : n_read;
		data[n_pitched].a[BLK_W] = ((Ctrl->W.weighted[GMT_IN]) ? ((Ctrl->W.sigma[GMT_IN]) ? 1.0 / in[3] : in[3]) : 1.0);
		if (!Ctrl->C.active) {	/* Need to store (x,y) so we can compute median location later */
			data[n_pitched].a[GMT_X] = in[GMT_X];
			data[n_pitched].a[GMT_Y] = in[GMT_Y];
		}
		data[n_pitched].a[BLK_Z] = in[GMT_Z];

		n_pitched++;
	} while (true);

	GMT->session.min_meminc = GMT_MIN_MEMINC;		/* Reset to the default value */

	if (GMT_End_IO (API, GMT_IN, 0) != GMT_NOERROR) {	/* Disables further data input */
		Return (API->error);
	}

	if (n_read == 0) {	/* Blank/empty input files */
		GMT_Report (API, GMT_MSG_VERBOSE, "No data records found; no output produced\n");
		Return (GMT_NOERROR);
	}
	if (n_pitched == 0) {	/* No points inside region */
		GMT_Report (API, GMT_MSG_VERBOSE, "No data points found inside the region; no output produced\n");
		Return (GMT_NOERROR);
	}

	if (n_pitched < n_alloc) {
		n_alloc = n_pitched;
		data = gmt_M_malloc (GMT, data, 0, &n_alloc, struct BLK_DATA);
	}

	/* Ready to go. */

	if (GMT_Begin_IO (API, GMT_IS_DATASET, GMT_OUT, GMT_HEADER_ON) != GMT_NOERROR) {	/* Enables data output and sets access mode */
		gmt_M_free (GMT, data);
		Return (API->error);
	}
	if (GMT_Set_Geometry (API, GMT_OUT, GMT_IS_POINT) != GMT_NOERROR) {	/* Sets output geometry */
		Return (API->error);
	}

	w_col = gmt_get_cols (GMT, GMT_OUT) - 1;	/* Weights always reported in last output column */
	if (emode) {					/* Index column last, with weight col just before */
		i_col = w_col--;
		old_format = GMT->current.io.o_format[i_col];		/* Need to restore this at end */
		GMT->current.io.o_format[i_col] = strdup ("%.0f");	/* Integer format for src_id */
	}

	/* Sort on node and Z value */

	qsort (data, n_pitched, sizeof (struct BLK_DATA), BLK_compare_index_z);

	/* Find n_in_cell and write appropriate output  */

	first_in_cell = n_cells_filled = nz = 0;
	while (first_in_cell < n_pitched) {
		weight = data[first_in_cell].a[BLK_W];
		if (do_extra) {
			if (nz == nz_alloc) z_tmp = gmt_M_malloc (GMT, z_tmp, nz, &nz_alloc, double);
			z_tmp[0] = data[first_in_cell].a[BLK_Z];
			nz = 1;
		}
		first_in_new_cell = first_in_cell + 1;
		while ((first_in_new_cell < n_pitched) && (data[first_in_new_cell].ij == data[first_in_cell].ij)) {
			weight += data[first_in_new_cell].a[BLK_W];
			if (do_extra) {	/* Must get a temporary copy of the sorted z array */
				if (nz == nz_alloc) z_tmp = gmt_M_malloc (GMT, z_tmp, nz, &nz_alloc, double);
				z_tmp[nz++] = data[first_in_new_cell].a[BLK_Z];
			}
			first_in_new_cell++;
		}

		/* Now we have weight sum [and copy of z in case of -E]; now calculate the quantile(s): */

		median_output (GMT, Grid->header, first_in_cell, first_in_new_cell, weight, out, extra, go_quickly, emode, quantile, n_quantiles, data);
		/* Here, x,y,z are loaded into out */

		if (Ctrl->E.mode & BLK_DO_EXTEND4) {	/* Need 7 items: x, y, median, min, 25%, 75%, max [,weight] */
			out[3] = z_tmp[0];	/* 0% quantile (min value) */
			out[4] = extra[0];	/* 25% quantile */
			out[5] = extra[2];	/* 75% quantile */
			out[6] = z_tmp[nz-1];	/* 100% quantile (max value) */
		}
		else if (Ctrl->E.mode & BLK_DO_EXTEND3) {	/* Need 6 items: x, y, median, MAD, min, max [,weight] */
			out[4] = z_tmp[0];	/* Low value */
			out[5] = z_tmp[nz-1];	/* High value */
			/* Turn z_tmp into absolute deviations from the median (out[GMT_Z]) */
			if (nz > 1) {
				for (node = 0; node < nz; node++) z_tmp[node] = fabs (z_tmp[node] - out[GMT_Z]);
				gmt_sort_array (GMT, z_tmp, nz, GMT_DOUBLE);
				out[3] = (nz%2) ? z_tmp[nz/2] : 0.5 * (z_tmp[(nz-1)/2] + z_tmp[nz/2]);
				out[3] *= 1.4826;	/* This will be L1 MAD-based scale */
			}
			else
				out[3] = GMT->session.d_NaN;
		}
		if (Ctrl->W.weighted[GMT_OUT]) out[w_col] = (Ctrl->W.sigma[GMT_OUT]) ? 1.0 / weight : weight;
		if (emode) out[i_col] = extra[3];

		GMT_Put_Record (API, GMT_WRITE_DATA, out);	/* Write this to output */

		n_cells_filled++;
		first_in_cell = first_in_new_cell;
	}

	gmt_M_free (GMT, data);
	if (do_extra) gmt_M_free (GMT, z_tmp);

	if (GMT_End_IO (API, GMT_OUT, 0) != GMT_NOERROR) {	/* Disables further data output */
		Return (API->error);
	}

	n_lost = n_read - n_pitched;	/* Number of points that did not get used */
	GMT_Report (API, GMT_MSG_VERBOSE, "N read: %" PRIu64 " N used: %" PRIu64 " outside_area: %" PRIu64 " N cells filled: %" PRIu64 "\n", n_read, n_pitched, n_lost, n_cells_filled);

	if (emode) {
		gmt_M_str_free (GMT->current.io.o_format[i_col]);	/* Free the temporary integer format */
		GMT->current.io.o_format[i_col] = old_format;		/* Restore previous format */
	}

	Return (GMT_NOERROR);
}
