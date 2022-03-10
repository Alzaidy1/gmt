#!/usr/bin/env bash
# Test a categorical CPT with text keys on a shapefile using
# the aspatial field NAME via the CPT to yield pen color
ps=categorical.ps
# get shapefile from cache
test_data=`gmt which -Gl @RidgeTest.shp`
gmt which -Gl @RidgeTest.shx @RidgeTest.dbf @RidgeTest.prj
# Make a text-based categorical cpt file
cat << EOF > ridge.cpt
Reykjanes	red
Klitgord	green
Dietmar		blue
EOF
gmt psxy -R-40/-25/30/45 -JM6i -P -Baf RidgeTest.shp -aZ=NAME -Cridge.cpt -K -Xc > $ps
gmt psxy -R -J -O -Sc0.2i -Cridge.cpt << EOF >> $ps
-37	33	Dietmar
-32	38	Klitgord
-30	42	Reykjanes
EOF
