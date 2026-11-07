#!/usr/bin/env bash
# capture-hand.sh: close-ups of the sword hand into out/hand/ (idle from the right and the front, and mid-combo), for
# checking the grip by eye (development, software GL)
W="$(cd "$(dirname "$0")/../../.." && pwd)"; mkdir -p $W/out/hand
S=$W/games/adventure/tools/swshot.sh
ADV_NO_HUD=1 ADV_FROM=1 ADV_CAM_DIST=1.4 ADV_CAM_PITCH=0.05 ADV_CAM_YAW=1.6 $S adventure_auto 160 $W/out/hand/a.png >/dev/null 2>&1 &
ADV_NO_HUD=1 ADV_FROM=1 ADV_CAM_DIST=1.4 ADV_CAM_PITCH=0.05 ADV_CAM_YAW=2.6 $S adventure_auto 160 $W/out/hand/b.png >/dev/null 2>&1 &
wait
ADV_NO_HUD=1 ADV_FROM=6 ADV_CAM_DIST=2.0 ADV_CAM_PITCH=0.0 ADV_CAM_YAW=1.6 $S adventure_auto 420 $W/out/hand/c.png >/dev/null 2>&1 &
ADV_NO_HUD=1 ADV_FROM=6 ADV_CAM_DIST=2.0 ADV_CAM_PITCH=0.0 ADV_CAM_YAW=1.6 $S adventure_auto 433 $W/out/hand/d.png >/dev/null 2>&1 &
wait
ls $W/out/hand
