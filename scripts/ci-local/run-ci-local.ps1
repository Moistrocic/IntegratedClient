# Local replication of the GitHub Actions workflows using nektos/act.
#
# Linux jobs run in catthehacker/ubuntu runner images; Windows jobs run in act
# host mode on this machine. macOS/iOS jobs cannot be reproduced locally.
#
# See scripts/ci-local/README.md for the coverage matrix and limitations.

[CmdletBinding()]
param(
	# Workflow file name ("style", "build", ...) or a path to a workflow YAML.
	[string]$Workflow,

	# Run a single job id from the workflow (-j).
	[string]$Job,

	[ValidateSet("push", "pull_request", "merge_group")]
	[string]$Event = "push",

	# Extra matrix filters, e.g. -Matrix os:windows-latest -Matrix name:windows-latest
	[string[]]$Matrix = @(),

	# Local parallelism cap. AGENTS.md requires <= 8 for local heavy work.
	[ValidateRange(1, 8)]
	[int]$CpuLimit = 8,

	# Only list jobs that act sees in the workflow.
	[switch]$List,

	# Do not force-pull the runner image on every run.
	[switch]$NoPull,

	# Keep containers for reuse between runs (-r).
	[switch]$Reuse,

	# Override the act executable.
	[string]$ActBin
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $repoRoot

$actVersion = "v0.2.97"
$actDir = Join-Path $repoRoot "_work\tools\act\bin"
$workDir = Join-Path $repoRoot "_work\ci\act"
$logsDir = Join-Path $workDir "logs"
$artifactsDir = Join-Path $workDir "artifacts"
$cacheDir = Join-Path $workDir "cache"
$envFile = Join-Path $workDir "act.env"
$secretsFile = Join-Path $workDir "act.secrets"

if (-not $ActBin) { $ActBin = Join-Path $actDir "act.exe" }

function Ensure-Act {
	if (Test-Path $ActBin) { return }
	Write-Host "act not found, downloading $actVersion ..."
	New-Item -ItemType Directory -Force -Path $actDir | Out-Null
	if (Get-Command gh -ErrorAction SilentlyContinue) {
		gh release download $actVersion -R nektos/act --pattern "act_Windows_x86_64.zip" -D $actDir
	} else {
		Invoke-WebRequest -Uri "https://github.com/nektos/act/releases/download/$actVersion/act_Windows_x86_64.zip" -OutFile (Join-Path $actDir "act_Windows_x86_64.zip")
	}
	Expand-Archive -Path (Join-Path $actDir "act_Windows_x86_64.zip") -DestinationPath $actDir -Force
}

function Ensure-Docker {
	docker info *> $null
	if ($LASTEXITCODE -ne 0) {
		throw "Docker daemon 未运行：act 的 Linux 容器作业需要 Docker Desktop。"
	}
}

function Resolve-WorkflowPath([string]$Name) {
	# Prefer standard workflow names first so that a directory with the same
	# name (e.g. the "build" output directory) cannot shadow the workflow.
	$candidates = @(
		(Join-Path $repoRoot ".github\workflows\$Name.yml"),
		(Join-Path $repoRoot ".github\workflows\$Name.yaml")
	)
	foreach ($candidate in $candidates) {
		if (Test-Path $candidate -PathType Leaf) { return (Resolve-Path $candidate).Path }
	}
	if (Test-Path $Name -PathType Leaf) { return (Resolve-Path $Name).Path }
	throw "找不到 workflow：$Name（示例：style / build / rust / .github/workflows/style.yml）"
}

Ensure-Act
Ensure-Docker

foreach ($dir in @($workDir, $logsDir, $artifactsDir, $cacheDir)) {
	New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

if (-not (Test-Path $envFile)) {
	@(
		"CMAKE_BUILD_PARALLEL_LEVEL=$CpuLimit",
		"CARGO_BUILD_JOBS=$CpuLimit",
		"CTEST_PARALLEL_LEVEL=$CpuLimit",
		"ACT_LOCAL_CI=1"
	) | Set-Content -Path $envFile -Encoding utf8NoBOM
}

if (-not (Test-Path $secretsFile)) {
	@(
		"# Local stand-ins for workflow secrets. Fill in only what a workflow needs.",
		"# VERSION is used by fast-build/debug-build; empty means no versioninfo.h injection.",
		"VERSION=",
		"# release.yml refuses to build without this; keep empty unless you really release.",
		"BESTCLIENT_OFFICIAL_CODE=",
		"WINDOWS_CERT_BASE64=",
		"WINDOWS_CERT_PASSWORD="
	) | Set-Content -Path $secretsFile -Encoding utf8NoBOM
	Write-Host "created secrets template: $secretsFile"
}

if (-not $Workflow) {
	throw "请用 -Workflow 指定 workflow（如 style / build / rust），或用 -Workflow <path>。"
}

$workflowPath = Resolve-WorkflowPath $Workflow

$actArgs = @($Event, "-W", $workflowPath)
if ($Job) { $actArgs += @("-j", $Job) }
if ($List) { $actArgs += "--list" }
foreach ($entry in $Matrix) { $actArgs += @("--matrix", $entry) }

# Runner image mapping. Linux uses the community GitHub-runner images; Windows
# jobs execute on this machine via act host mode. macOS/iOS are intentionally
# unmapped so they fail loudly instead of pretending to work.
$actArgs += @(
	"-P", "ubuntu-latest=catthehacker/ubuntu:act-latest",
	"-P", "ubuntu-22.04=catthehacker/ubuntu:act-22.04",
	"-P", "ubuntu-24.04=catthehacker/ubuntu:act-24.04",
	"-P", "windows-latest=-self-hosted",
	"-P", "windows-2022=-self-hosted"
)

$actArgs += @(
	"--container-options", "--cpus=$CpuLimit",
	"--env-file", $envFile,
	"--secret-file", $secretsFile,
	"--artifact-server-path", $artifactsDir,
	"--cache-server-path", $cacheDir
)

if (-not $NoPull) { $actArgs += "--pull=false" }
if ($Reuse) { $actArgs += "-r" }

$logName = "{0}-{1}-{2}.log" -f [IO.Path]::GetFileNameWithoutExtension($workflowPath), $Event, (Get-Date -Format "yyyyMMdd-HHmmss")
$logPath = Join-Path $logsDir $logName

Write-Host "act: $ActBin"
Write-Host "workflow: $workflowPath"
Write-Host "event: $Event  cpu-limit: $CpuLimit  log: $logPath"
Write-Host ""

& $ActBin @actArgs 2>&1 | Tee-Object -FilePath $logPath
$exitCode = $LASTEXITCODE

Write-Host ""
Write-Host "log saved to: $logPath"
exit $exitCode
