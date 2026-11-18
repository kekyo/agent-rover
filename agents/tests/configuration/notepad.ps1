# agent-rover - Windows 11 Notepad test observation
# Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
# Under MIT.
param([string]$WindowId, [string]$Action, [string]$TabId = '')
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$window = [System.Windows.Automation.AutomationElement]::FromHandle([IntPtr][Convert]::ToInt64($WindowId, 16))
$scope = [System.Windows.Automation.TreeScope]::Descendants
$all = [System.Windows.Automation.Condition]::TrueCondition
$controls = $window.FindAll($scope, $all)
$tabs = @()
$selected = $null
$document = $null
$dialog = $false
foreach ($element in $controls) {
    $state = $element.Current
    if ($state.ControlType -eq [System.Windows.Automation.ControlType]::TabItem) {
        $id = $element.GetRuntimeId() -join ','
        $selection = $element.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
        $tabs += @{ id = $id; name = $state.Name; selected = $selection.Current.IsSelected }
        if ($selection.Current.IsSelected) { $selected = $element }
    }
    if ($state.ControlType -eq [System.Windows.Automation.ControlType]::Document) { $document = $element }
    if ($state.ControlType -eq [System.Windows.Automation.ControlType]::Button -and
        ($state.Name -eq 'OK' -or $state.Name -eq "Don't save")) { $dialog = $true }
}

# Read only the selected test document. Other user tabs may share this process.
$selectedId = ''
$text = $null
if ($selected -and $selected.Current.Name.StartsWith('.agent-rover-agent.ini.')) {
    $selectedId = $selected.GetRuntimeId() -join ','
    if ($document) {
        $pattern = $document.GetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern)
        $text = $pattern.DocumentRange.GetText(-1)
    }
}
if ($Action -ne 'inspect') {
    if ($dialog -or !$selectedId -or $selectedId -ne $TabId) { throw 'The selected test tab is not ready.' }
    if ($Action -eq 'select-all') {
        $document.SetFocus()
        $pattern.DocumentRange.Select()
    } elseif ($Action -eq 'close') {
        $button = @($selected.FindAll($scope, $all) | Where-Object {
            $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Button -and
            $_.Current.Name -eq 'Close Tab'
        })
        if ($button.Count -ne 1) { throw 'The selected test tab has no unique close button.' }
        $button[0].GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    } else { throw 'Unknown Notepad test action.' }
}
@{ tabs = $tabs; selectedId = $selectedId; text = $text; dialog = $dialog } | ConvertTo-Json -Depth 3 -Compress
