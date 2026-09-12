CD /D %~dp0
npm run build:android; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; npm run build:android:nofx; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; dir android\app\release\jh_ol*.apk; findstr /C:"versionCode" /C:"versionName" android\app\build.gradle; type client_code_version.txt
pause