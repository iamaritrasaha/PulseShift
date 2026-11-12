param([ValidateSet('demo','wokwi','capture','vehicle','all')][string]$Target='demo')
$ErrorActionPreference='Stop'
$env:TEMP='D:\Arduino\temp'
$env:TMP=$env:TEMP
$cli='D:\Tools\ArduinoCLI-1.5.1\arduino-cli.exe'
$config='D:\Arduino\arduino-cli.yaml'
$targets=if($Target -eq 'all') {@('demo','wokwi','capture','vehicle')} else {@($Target)}
foreach($variant in $targets) {
  $fqbn='esp32:esp32:esp32:FlashMode=dio,FlashFreq=40,UploadSpeed=115200'
  if($variant -eq 'wokwi') {$fqbn='esp32:esp32:esp32'}
  $flags='-std=gnu++17'
  if($variant -eq 'wokwi') {$flags+=' -DPULSESHIFT_OLED_ENABLED=1'}
  if($variant -eq 'capture') {$flags+=' -DPULSESHIFT_MODE=3'}
  if($variant -eq 'vehicle') {$flags+=' -DPULSESHIFT_MODE=4'}
  & $cli --config-file $config compile --fqbn $fqbn --warnings all --build-property "compiler.cpp.extra_flags=$flags" --build-path "$PSScriptRoot\cache\$variant" --output-dir "$PSScriptRoot\build\$variant" "$PSScriptRoot\PulseShift"
  if($LASTEXITCODE -ne 0) {throw "Compilation failed: $variant ($LASTEXITCODE)"}
}

