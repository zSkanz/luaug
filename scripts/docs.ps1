# Build the documentation site and open it.
#
# The site is a build product rather than a checked-in artifact, so this is how
# a person looks at it: one command, no server, no install. It opens from a
# file:// URL because every path in it is relative -- which is also what lets it
# be served from a subdirectory of a web server without configuration.
#
# `-Out` writes somewhere else; `-NoOpen` builds without opening, which is what
# a script wants.

param(
    [string]$Out = "",
    [switch]$NoOpen
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

try {
    $arguments = @('api/generator/gen_site.luau')
    if ($Out -ne "") {
        $arguments += "--out=$Out"
    }

    & lute @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "the documentation site did not build"
    }

    if ($Out -ne "") {
        $target = $Out
    } elseif ($env:LUAUG_BUILD_ROOT) {
        $target = Join-Path $env:LUAUG_BUILD_ROOT 'docs-site'
    } else {
        $target = Join-Path $repoRoot 'out/docs-site'
    }

    $index = Join-Path $target 'index.html'
    if (-not (Test-Path $index)) {
        throw "the generator reported success and wrote no index at $index"
    }

    if (-not $NoOpen) {
        Start-Process $index
    }
    Write-Host "docs: $index"
} finally {
    Pop-Location
}
