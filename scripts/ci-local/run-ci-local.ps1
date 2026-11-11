# Local replication of the GitHub Actions workflows using nektos/act.
#
# Linux jobs run in catthehacker/ubuntu runner images (or the locally extended
# ic-ci-runner:* images that add the tools GitHub-hosted runners preinstall);
# Windows jobs run in act host mode on this machine. macOS/iOS jobs cannot be
# reproduced locally.
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

	# Jobs act may run concurrently inside one invocation. act defaults to the
	# CPU count, which would stack several 8-way builds on this machine; keep it
	# serial unless you really need parallel jobs.
	[ValidateRange(1, 8)]
	[int]$ConcurrentJobs = 1,

	# Only list jobs that act sees in the workflow.
	[switch]$List,

	# Force-pull the runner image even if it exists locally.
	[switch]$ForcePull,

	# Re-fetch action repositories (uses:) from GitHub even when they are cached
	# in %USERPROFILE%\.cache\act. Default is act's --action-offline-mode: cached
	# actions are reused and only missing ones are fetched, which avoids the
	# flaky "git clone <action>" network failures seen on this machine.
	[switch]$RefreshActions,

	# Retry the invocation when it fails while fetching action repositories from
	# GitHub (network timeouts) or because the random artifact server port is
	# already in use. Only those two failure modes are retried.
	[ValidateRange(1, 5)]
	[int]$MaxAttempts = 2,

	# Bind the repository into Linux containers (act --bind) instead of copying
	# it. Much faster start-up for this multi-GB repo, but workflow steps then
	# write into the real working tree; only use it for read-only jobs.
	[switch]$Bind,

	# Keep act's per-run host workspaces (%USERPROFILE%\.cache\act\<hex>\hostexecutor).
	# By default they are removed after the run when act could not delete them
	# itself (Windows file locks often leave several GB per host-mode build).
	[switch]$KeepHostWorkspaces,

	# Keep containers for reuse between runs (-r).
	[switch]$Reuse,

	# Override the act executable.
	[string]$ActBin,

	# Use the locally built GitHub-runner approximation (ic-ci-runner:*) instead
	# of the bare catthehacker images. Missing tags fall back to the base image.
	[switch]$UseCustomRunner,

	# Build the local runner images before running (requires Docker + network).
	[switch]$BuildRunnerImages,

	# Which Ubuntu runner images to build: latest / 22.04 / 24.04.
	[ValidateSet("latest", "22.04", "24.04")]
	[string[]]$RunnerImages = @("latest")
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

# Log substrings that mean "the mutation was caused by a transient infrastructure
# problem", not by the repository. Used to decide whether a retry makes sense.
$networkFailurePattern = 'Unable to clone https://github\.com|dial tcp|connection attempt failed|wsarecv|i/o timeout|TLS handshake timeout|Could not resolve host|Connection was reset|connection reset by peer'
$portInUsePattern = 'Only one usage of each socket address|bind: address already in use'

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

function Test-DockerImage([string]$Image) {
	if (-not (Get-Command docker -ErrorAction SilentlyContinue)) { return $false }
	docker image inspect $Image *> $null
	return ($LASTEXITCODE -eq 0)
}

function Test-DockerAvailable {
	if (Get-Command docker -ErrorAction SilentlyContinue) {
		docker info *> $null
		if ($LASTEXITCODE -eq 0) { return $true }
	}
	Write-Warning "Docker daemon 不可用：Ubuntu 容器作业会失败；Windows host 模式作业仍可运行。"
	return $false
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

function Build-RunnerImages {
	$baseByTag = @{
		"latest" = "catthehacker/ubuntu:act-latest"
		"22.04"  = "catthehacker/ubuntu:act-22.04"
		"24.04"  = "catthehacker/ubuntu:act-24.04"
	}
	$dockerfile = Join-Path $PSScriptRoot "runner\Dockerfile"
	foreach ($tag in $RunnerImages) {
		$base = $baseByTag[$tag]
		Write-Host "building ic-ci-runner:$tag from $base ..."
		docker build -f $dockerfile --build-arg "BASE_IMAGE=$base" -t "ic-ci-runner:$tag" (Join-Path $PSScriptRoot "runner")
		if ($LASTEXITCODE -ne 0) { throw "failed to build ic-ci-runner:$tag" }
	}
}

function Get-FreeTcpPort {
	# Let the OS pick a currently free port; the listener is closed immediately
	# before act starts its artifact server on it.
	$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
	$listener.Start()
	try {
		return ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
	} finally {
		$listener.Stop()
	}
}

function Set-ActArgValue([string[]]$Arguments, [string]$Name, [string]$Value) {
	$index = [Array]::IndexOf($Arguments, $Name)
	if ($index -ge 0 -and ($index + 1) -lt $Arguments.Count) {
		$Arguments[$index + 1] = $Value
	}
}

function Clear-ActHostWorkspaces {
	# act creates a random <16 hex>/hostexecutor workspace for every host-mode
	# job and removes it in its cleanup callback. On Windows that removal often
	# fails while compiler/MSBuild handles are still open, leaving several GB
	# behind. Delete those leftovers after our run when no other act is active.
	if (@(Get-Process act -ErrorAction SilentlyContinue).Count -gt 0) {
		Write-Warning "另一个 act 进程仍在运行，跳过 host workspace 清理。"
		return
	}
	$actCacheRoot = Join-Path $env:USERPROFILE ".cache\act"
	if (-not (Test-Path $actCacheRoot)) { return }
	foreach ($dir in Get-ChildItem -Path $actCacheRoot -Directory -ErrorAction SilentlyContinue) {
		if ($dir.Name -notmatch '^[0-9a-f]{16}$') { continue }
		if (-not (Test-Path (Join-Path $dir.FullName "hostexecutor"))) { continue }
		$size = (Get-ChildItem $dir.FullName -Recurse -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
		Write-Host ("removing stale act host workspace {0} ({1:N1} GB) ..." -f $dir.FullName, ($size / 1GB))
		Remove-Item -Path $dir.FullName -Recurse -Force -ErrorAction SilentlyContinue
		if (Test-Path $dir.FullName) {
			Write-Warning "无法完全删除 $($dir.FullName)（文件仍被占用），可稍后手动清理。"
		}
	}
}

function Get-RunnerMappings {
	$runners = @(
		@{ Name = "ubuntu-latest"; Custom = "ic-ci-runner:latest"; Base = "catthehacker/ubuntu:act-latest" },
		@{ Name = "ubuntu-22.04"; Custom = "ic-ci-runner:22.04"; Base = "catthehacker/ubuntu:act-22.04" },
		@{ Name = "ubuntu-24.04"; Custom = "ic-ci-runner:24.04"; Base = "catthehacker/ubuntu:act-24.04" }
	)
	$mappings = @()
	foreach ($runner in $runners) {
		$image = $runner.Base
		if ($UseCustomRunner) {
			if ($dockerAvailable -and (Test-DockerImage $runner.Custom)) {
				$image = $runner.Custom
			} else {
				Write-Warning "custom runner image '$($runner.Custom)' 不在本地，回退到 '$($runner.Base)'（用 -BuildRunnerImages -RunnerImages $($runner.Name -replace '^ubuntu-', '') 构建）。"
			}
		}
		$mappings += @("-P", "$($runner.Name)=$image")
	}
	# Windows jobs execute on this machine via act host mode. macOS/iOS are
	# intentionally unmapped so they fail loudly instead of pretending to work.
	$mappings += @("-P", "windows-latest=-self-hosted", "-P", "windows-2022=-self-hosted")
	return $mappings
}

$dockerAvailable = Test-DockerAvailable

if ($BuildRunnerImages) {
	if (-not $dockerAvailable) { throw "Docker 不可用，无法构建 runner 镜像。" }
	Build-RunnerImages
}

Ensure-Act

foreach ($dir in @($workDir, $logsDir, $artifactsDir, $cacheDir)) {
	New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

if (-not (Test-Path $envFile)) {
	@(
		"CMAKE_BUILD_PARALLEL_LEVEL=$CpuLimit",
		"CARGO_BUILD_JOBS=$CpuLimit",
		"CTEST_PARALLEL_LEVEL=$CpuLimit",
		"ACT_LOCAL_CI=1",
		"# Keep MSBuild worker nodes from lingering: they hold handles on the host",
		"# workspace copy and make act's workspace cleanup fail on Windows.",
		"MSBUILDDISABLENODEREUSE=1"
	) | Set-Content -Path $envFile -Encoding utf8NoBOM
} elseif ((Get-Content -Path $envFile -Raw) -notmatch '(?m)^MSBUILDDISABLENODEREUSE=') {
	Add-Content -Path $envFile -Value "MSBUILDDISABLENODEREUSE=1" -Encoding utf8NoBOM
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

$actArgs += Get-RunnerMappings

# act's default artifact server port (34567) collides when two act runs happen at
# once, and an empty value yields artifact URLs like "http://host:" that make
# actions/upload-artifact fail with ECONNREFUSED. Allocate a free port per run.
$artifactPort = Get-FreeTcpPort

$actArgs += @(
	"--container-options", "--cpus=$CpuLimit",
	"--concurrent-jobs", "$ConcurrentJobs",
	"--env-file", $envFile,
	"--secret-file", $secretsFile,
	"--artifact-server-path", $artifactsDir,
	"--artifact-server-port", "$artifactPort",
	"--cache-server-path", $cacheDir
)

if ($ForcePull) { $actArgs += "--pull" } else { $actArgs += "--pull=false" }
if ($Reuse) { $actArgs += "-r" }
if ($Bind) { $actArgs += "--bind" }
if (-not $RefreshActions) { $actArgs += "--action-offline-mode" }

$logParts = @([IO.Path]::GetFileNameWithoutExtension($workflowPath), $Event)
if ($Job) { $logParts += ($Job -replace '[^A-Za-z0-9._-]', '_') }
if ($Matrix.Count -gt 0) { $logParts += (($Matrix -join ',') -replace '[^A-Za-z0-9._-]', '_') }
$logParts += (Get-Date -Format "yyyyMMdd-HHmmss")
$logBase = $logParts -join '-'
$logPath = Join-Path $logsDir "$logBase.log"

Write-Host "act: $ActBin"
Write-Host "workflow: $workflowPath"
Write-Host "event: $Event  cpu-limit: $CpuLimit  concurrent-jobs: $ConcurrentJobs  log: $logPath"
Write-Host ""

$attempt = 1
$portRetries = 0
while ($true) {
	$attemptLog = $logPath
	if ($attempt -gt 1) { $attemptLog = Join-Path $logsDir "$logBase-attempt$attempt.log" }

	Write-Host "== act attempt $attempt/$MaxAttempts (log: $attemptLog) =="
	& $ActBin @actArgs 2>&1 | Tee-Object -FilePath $attemptLog
	$exitCode = $LASTEXITCODE
	if ($exitCode -eq 0) { break }

	$logText = ""
	if (Test-Path $attemptLog) { $logText = Get-Content -Path $attemptLog -Raw -ErrorAction SilentlyContinue }

	if ($portRetries -lt 3 -and $logText -match $portInUsePattern) {
		$portRetries++
		$artifactPort = Get-FreeTcpPort
		Set-ActArgValue -Arguments $actArgs -Name "--artifact-server-port" -Value "$artifactPort"
		Write-Warning "artifact server 端口被占用，换端口 $artifactPort 重试 ..."
		continue
	}

	if ($attempt -lt $MaxAttempts -and $logText -match $networkFailurePattern) {
		Write-Warning "act 拉取 GitHub action 时网络失败，重试（$($attempt + 1)/$MaxAttempts）..."
		$attempt++
		Start-Sleep -Seconds 5
		continue
	}

	break
}

if (-not $KeepHostWorkspaces) {
	Clear-ActHostWorkspaces
}

Write-Host ""
Write-Host "log saved to: $logPath"
exit $exitCode
