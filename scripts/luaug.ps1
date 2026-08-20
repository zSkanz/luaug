# `luaug`, as a developer types it. See scripts/luaug.sh for why this is a
# wrapper around `lute` rather than a compiled binary (M3 brief, Decision 2).
$repo = Split-Path -Parent $PSScriptRoot
& lute run (Join-Path $repo 'tools/cli/main.luau') -- @args
exit $LASTEXITCODE
