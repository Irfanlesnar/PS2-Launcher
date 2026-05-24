#!/bin/bash
# Total cleanup: delete absolutely every image in gfx/ except loader.png
if [ -f gfx/loader.png ]; then
    echo "Safeguarding gfx/loader.png..."
    mv gfx/loader.png ./loader_temp.png
    echo "Purging all other PNGs..."
    rm -f gfx/*.png
    mv ./loader_temp.png gfx/loader.png
    echo "Purge complete! Only gfx/loader.png remains in the gfx folder."
else
    echo "Error: gfx/loader.png not found. Please make sure you run this from the project root."
fi
