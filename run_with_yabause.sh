#!/bin/bash
command -v /home/poberoi/yabause15/yabause/build/src/qt/yabause >/dev/null 2>&1 || { echo "yabause is not installed.\
 Aborting." >&2; exit 1; }

if [ -f game.iso ];
then
   /home/poberoi/yabause15/yabause/build/src/qt/yabause -a -i game.cue
else
   echo "Please compile first !" >&2
fi
