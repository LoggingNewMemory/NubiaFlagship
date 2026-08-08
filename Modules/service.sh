#!/system/bin/sh

# Wait until boot is complete
until [ "$(getprop sys.boot_completed)" = "1" ]; do
    sleep 2
done

# I Hate to say only this works _- I have no choice but to rely on this
sleep 15

su -c /data/adb/modules/NubiaNeo3GT5GFix/FODAnimFix