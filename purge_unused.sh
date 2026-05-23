#!/bin/bash
# OPL Cleanup Script - Purge Unused Multi-Language Files & Assets
echo "==========================================="
echo "  OPL-Fresh Repository Optimization Purge  "
echo "==========================================="

# 1. Purge multi-language translation files (.lng) to keep English only
if [ -d "lng" ]; then
    echo "🧹 Purging unused translation files from lng/..."
    find lng/ -type f -name "*.lng" -delete
    echo "✅ Cleaned up lng/ folder."
fi

# 2. Clean up translation template folders if they exist
for dir in lng_src lng_tmpl; do
    if [ -d "$dir" ]; then
        echo "🧹 Purging translation helper folder: $dir/..."
        rm -rf "$dir"
    fi
done

# 3. Purge old audio raw leftovers
if [ -d "audio" ]; then
    echo "🧹 Clean up audio/ raw leftovers..."
    find audio/ -type f ! -name "cancel.adp" ! -name "confirm.adp" ! -name "cursor.adp" -delete
fi

# 4. Success message
echo "==========================================="
echo "🎉 Cleanup complete! Repository is lean and optimized!"
echo "==========================================="
