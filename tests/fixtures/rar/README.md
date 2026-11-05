These two small RAR5 volumes contain one stored file that spans both volumes.
`clip.mp4` is test data: the ASCII string `0123456789abcdef` repeated 384 times.
To regenerate in an empty directory using PowerShell and RAR:

```powershell
[IO.File]::WriteAllBytes((Join-Path $PWD 'clip.mp4'), [Text.Encoding]::ASCII.GetBytes('0123456789abcdef' * 384))
rar a -m0 -ma5 -v4k -ep -tsm- -qo- sample.rar clip.mp4
```

The tests rename copies of these fixtures. Running them does not require RAR.
