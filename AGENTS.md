# SI Generator 작업 지침

- 사용자가 별도로 다르게 지시하지 않는 한, 수정 작업은 검증 후 커밋하고 GitHub에 푸시하며 PR을 main에 병합하는 데까지 완료한다.
- 병합한 커밋의 Windows Release 빌드와 회귀 검사가 성공했는지 확인한 뒤, 해당 커밋의 배포 ZIP으로 C:\SI-Generator 포터블도 갱신한다. 소스만 갱신하고 작업을 끝내지 않는다.
- `native/install-portable.ps1 -PackagePath <배포 ZIP> -SourceCommit <병합 커밋 SHA>`로 설치한다. 바탕화면의 SI Generator 바로가기는 C:\SI-Generator\SIGenerator.exe를 가리켜야 하며, 작업 폴더와 아이콘도 C드라이브 경로를 사용한다.
- EXE 하나만 복사하지 않는다. Qt DLL과 plugins 등 배포 ZIP의 전체 런타임이 필요하다. 외장하드와 개발 도구가 없어도 실행할 수 있어야 한다.
- 설치 후 배포 EXE의 해시, 바로가기 대상과 작업 폴더, C드라이브 포터블의 시작을 확인한다. 실행 중인 사용자 앱을 강제 종료하거나 사용자 프로젝트·설정을 삭제하지 않는다.
- C드라이브 포터블은 프로그램을 독립 실행하기 위한 복사본이다. 기존 E드라이브의 사용자 프로젝트와 Gaussian 원본까지 자동으로 옮기는 작업은 포함하지 않는다.
- 단일 EXE 배포를 요청받은 경우, 같은 Windows 빌드의 `SI-Generator-Single-File-Windows-x64` 산출물을 받아 `install-portable.ps1`에 `-SingleFilePath <SI generator.exe>`를 함께 전달한다. 바탕화면에는 `SI generator.exe` 하나를 두고 기존 SI Generator/SIGenerator 바로가기는 백업 후 교체한다. 이 경우에만 위의 바로가기 지침 대신 단일 EXE를 사용한다.
- 단일 EXE는 전체 Qt 런타임을 LZX 압축해 내장하며 `%LOCALAPPDATA%\SI-Generator\runtime`에 버전별로 검증·캐시한다. ZIP 포터블도 함께 갱신하고, 기존 프로젝트·설정과 실행 중인 앱은 보존한다.
