' Silent launcher — window style 0 = hidden (no empty console flash)
' Args: full command line to run
If WScript.Arguments.Count < 1 Then WScript.Quit 1
CreateObject("Wscript.Shell").Run WScript.Arguments(0), 0, False
