param(
    [Parameter(Mandatory)]
    [ValidateSet('Check', 'Package', 'Publish')]
    [string]$Action
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Native([string]$Command, [string[]]$Arguments) {
    $output = & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command $($Arguments -join ' ') failed ($LASTEXITCODE)"
    }
    $output
}

function Get-Version {
    $version = (Get-Content -LiteralPath 'build/x86-release/version.txt' -Raw).Trim()
    if ($version -notmatch '^\d+\.\d+\.\d+$') {
        throw "Invalid CMake release version: $version"
    }
    $version
}

function Get-Commit {
    Invoke-Native git @('rev-parse', 'HEAD')
}

function Assert-Clean {
    if (Invoke-Native git @('status', '--porcelain', '--untracked-files=no')) {
        throw 'Release requires a clean tracked working tree.'
    }
}

function Get-Repository {
    $remote = Invoke-Native git @('remote', 'get-url', 'origin')
    Invoke-Native gh @('repo', 'view', $remote, '--json', 'nameWithOwner', '--jq', '.nameWithOwner')
}

function Assert-Unreleased([string]$Tag, [string]$Repository) {
    if (Invoke-Native git @('ls-remote', '--tags', 'origin', "refs/tags/$Tag")) {
        throw "Remote tag $Tag already exists. Bump the CMake version."
    }
    $tags = @(Invoke-Native gh @('api', '--paginate', "repos/$Repository/releases?per_page=100", '--jq', '.[].tag_name'))
    if ($tags -contains $Tag) {
        throw "Release $Tag already exists. Bump the CMake version."
    }
}

function Get-ArchiveNames([string]$Version) {
    @("MWPerfFix-$Version-win32.zip", "MWPerfFix-$Version-win32-diagnostic.zip", "MWPerfFix-$Version-symbols.zip")
}

function New-Packages([string]$Version) {
    # Only this task's staging directory is disposable.
    $dist = Join-Path (Get-Location).Path 'build/dist'
    foreach ($path in @('build', $dist)) {
        if ((Test-Path -LiteralPath $path) -and
            ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Package directories must not be links: $path"
        }
    }
    if (Test-Path -LiteralPath $dist) {
        Remove-Item -LiteralPath $dist -Recurse -Force
    }
    New-Item -ItemType Directory -Path $dist | Out-Null
    $names = Get-ArchiveNames $Version
    $shared = @('README.md', 'LICENSE', 'MWPerfFix.ini')
    Compress-Archive -LiteralPath (@('build/x86-release/MWPerfFix.asi') + $shared) -DestinationPath "$dist/$($names[0])"
    Compress-Archive -LiteralPath (@('build/x86-diagnostic/MWPerfFix.asi', 'diagnostics/MWPerfFix.wprp') + $shared) -DestinationPath "$dist/$($names[1])"
    $symbols = "$dist/symbols"
    New-Item -ItemType Directory -Path "$symbols/normal", "$symbols/diagnostic" | Out-Null
    Copy-Item -LiteralPath 'build/x86-release/MWPerfFix.pdb' -Destination "$symbols/normal/"
    Copy-Item -LiteralPath 'build/x86-diagnostic/MWPerfFix.pdb' -Destination "$symbols/diagnostic/"
    Compress-Archive -Path "$symbols/*" -DestinationPath "$dist/$($names[2])"
}

function Publish-Release([string]$Version) {
    Assert-Clean
    $repository = Get-Repository
    $tag = "v$Version"
    Assert-Unreleased $tag $repository
    $commit = Get-Commit
    $archives = @(Get-ArchiveNames $Version | ForEach-Object { "build/dist/$_" })
    $notes = '.github/release-template.md'
    if (-not (Test-Path -LiteralPath $notes -PathType Leaf)) {
        throw "Missing release template: $notes"
    }
    foreach ($archive in $archives) {
        if (-not (Test-Path -LiteralPath $archive -PathType Leaf) -or
            (Get-Item -LiteralPath $archive).Length -eq 0) {
            throw "Missing or empty release archive: $archive"
        }
    }
    Invoke-Native git @('-c', 'user.name=github-actions[bot]', '-c', 'user.email=41898282+github-actions[bot]@users.noreply.github.com', 'tag', '-a', $tag, $commit, '-m', "MWPerfFix $Version")
    Invoke-Native git @('push', 'origin', "refs/tags/$tag")
    try {
        Invoke-Native gh (@('release', 'create', $tag, '--repo', $repository, '--verify-tag', '--notes-file', $notes) + $archives)
    } catch {
        throw "Tag $tag was pushed. Keep it. If a draft release exists, upload the archives with gh release upload and publish it with gh release edit --draft=false. Otherwise retry: gh release create $tag --repo $repository --verify-tag --notes-file $notes $($archives -join ' '). Original error: $_"
    }
}

switch ($Action) {
    Check {
        Assert-Clean
        Assert-Unreleased "v$(Get-Version)" (Get-Repository)
    }
    Package { New-Packages (Get-Version) }
    Publish { Publish-Release (Get-Version) }
}
