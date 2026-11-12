$env:TEMP='D:\Arduino\temp'
$env:TMP=$env:TEMP
$env:ARDUINO_DIRECTORIES_DATA='D:\Arduino\data'
$env:ARDUINO_DIRECTORIES_DOWNLOADS='D:\Arduino\downloads'
$env:ARDUINO_DIRECTORIES_USER='D:\Arduino\sketchbook'
$env:ARDUINO_BUILD_CACHE_PATH='D:\Arduino\cache'
Start-Process -FilePath 'D:\Tools\ArduinoIDE-2.3.10\Arduino IDE.exe' -ArgumentList @('--user-data-dir=D:\Arduino\ide-user-data','D:\Development\PulseShift-Firmware\PulseShift\PulseShift.ino') -WindowStyle Hidden -RedirectStandardOutput 'D:\Arduino\ide-stdout.log' -RedirectStandardError 'D:\Arduino\ide-stderr.log'
