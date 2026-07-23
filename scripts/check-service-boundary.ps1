param(
    [string]$Client = "bin/main.exe",
    [string]$Server = "bin/jh-online-server.exe"
)

$ErrorActionPreference = 'Stop'

function Require-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Boundary check requires build artifact: $Path"
    }
}

function Get-SymbolText([string]$Path) {
    return (& nm -C $Path 2>$null | Out-String)
}

function Assert-NoSymbol([string]$SymbolText, [string]$Pattern, [string]$Owner) {
    if ($SymbolText -match $Pattern) {
        throw "$Owner must not contain boundary-forbidden symbol pattern: $Pattern"
    }
}

Require-File $Client
Require-File $Server

$clientSymbols = Get-SymbolText $Client
$serverSymbols = Get-SymbolText $Server
$serverImports = (& objdump -p $Server 2>$null | Out-String)
$clientStrings = (& strings -a $Client 2>$null | Out-String)

# The CBE client may have a socket transport but must not embed protocol
# builders, the game listener, the administration surface, or MySQL access.
Assert-NoSymbol $clientSymbols 'vm_net_mock_service_run_forever' 'client'
Assert-NoSymbol $clientSymbols 'vm_mock_admin_' 'client'
Assert-NoSymbol $clientSymbols 'vm_mysql_' 'client'
Assert-NoSymbol $clientSymbols 'vm_net_mock_build_[A-Za-z0-9_]*response' 'client'
# The local cache is intentionally named JHOnlineData.  The service catalog
# lives under web/fs/JHOnlineData and must only be reached through WT18/7, not
# by a co-located client opening the server deployment directory.
Assert-NoSymbol $clientStrings '(?i)(\.\./)?web/fs/JHOnlineData/' 'client'

# The service must not contain the CBE-side request/response bridge.  Its only
# ingress/egress is CBMS socket bytes handled by the service listener.
Assert-NoSymbol $serverSymbols 'vm_net_mock_on_send' 'server'
Assert-NoSymbol $serverSymbols 'vm_net_mock_read_data' 'server'
Assert-NoSymbol $serverSymbols 'vm_client_remote_request' 'server'
Assert-NoSymbol $serverSymbols '\b(uc_mem_(read|write)|uc_reg_(read|write)|g_cbeInfo|Global_R9)\b' 'server'
# Catch the wrapper layer as well as the raw Unicorn calls: these APIs all
# consume CBE guest pointers or client-local resource/cache state.
Assert-NoSymbol $serverSymbols '\b(vm_(get|set)_var|vm_readString|vm_read_path_string|vm_cbfs_|vm_DF_|vm_IMG_|vm_file_try_download_named_resource|vm_resource_cache_)' 'server'
Assert-NoSymbol $serverImports '(?i)(unicorn|SDL)' 'server imports'

Write-Output 'service boundary check passed: client=remote-CBMS-only-with-local-cache server=no-client-transport-or-VM-runtime-imports'
