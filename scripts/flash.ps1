# Run from an activated Python virtual environment. All arguments go to the guarded flasher.
& python (Join-Path $PSScriptRoot "be.py") flash @args
exit $LASTEXITCODE
