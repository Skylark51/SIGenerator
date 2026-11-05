# Native SI Generator

## Build

```powershell
cmake -S native -B native/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=E:\Qt\6.8.3\msvc2022_64
cmake --build native/build --config Release --parallel 4
powershell -ExecutionPolicy Bypass -File native/deploy.ps1
```

The deployable application is `native/build/Release/SIGenerator.exe`. It has no Python runtime dependency.

## Validation

```powershell
C:\Program Files\CMake\bin\ctest.exe --test-dir native/build -C Release --output-on-failure
native/build/Release/si_benchmark.exe --synthetic-100
```

The regression test covers Gaussian SCF energy, Mulliken spin density, TD-DFT excited states, and IRC direction/point extraction.

## Measured native parser timings

Measurements below are from the Windows Release build on this workstation using buffered single-pass synthetic Gaussian files. They include a cold sequential parse and a cache hit.

| Input | Cold parse | Cache hit |
| --- | ---: | ---: |
| 10 MB | 112.142 ms | 0.0813 ms |
| 50 MB | 546.180 ms | 0.0596 ms |
| 100 MB | 1076.310 ms | 0.0743 ms |

The synthetic files exercise the streaming path; production files additionally parse the final orientation, energies, frequencies, spin-density blocks, TD-DFT states, and IRC points in the same scan.


## 로컬 프로젝트 버전 이력

- **파일 → 프로젝트 버전 이력**에서 현재 상태를 설명과 함께 저장합니다. 저장된 프로젝트는 SI DOCX와 XYZ를 모두 생성했을 때도 버전이 추가됩니다.
- 기준과 비교 대상을 선택하면 구조 추가·삭제·이름·순서, 에너지, Figure, 단계, SPE 설정 변경을 확인할 수 있습니다.
- 복원하면 현재 편집 상태를 복원 직전 버전으로 저장한 뒤 선택한 상태를 프로젝트 파일에 적용합니다. 프로젝트 파일과 옆의 .json.history 폴더를 함께 이동하면 이력도 이동합니다.
- 이력에는 프로젝트 설정과 추출 스냅샷이 들어갑니다. 원본 Gaussian 파일 및 생성한 DOCX·XYZ의 내용은 버전 저장 대상에서 제외됩니다. 복원 후 원본이 있으면 일반 프로젝트 열기와 같이 원본을 우선 해석합니다.

## 모듈별 수정 방법

| 빌드 대상 | 기능 | 수정 진입점과 검증 |
| --- | --- | --- |
| si_calculation | Gaussian 해석, SPE 검증, 에너지와 기하 계산 | 해석 규칙은 src/GaussianParser.cpp, 계산은 src/SIModel.cpp. gaussian_regression 실행. |
| si_project | 프로젝트 JSON 호환성, 추출 스냅샷, 로컬 버전 저장·비교·복원 | 형식은 src/ProjectStore.cpp, 버전 정책은 src/ProjectHistory.cpp. project_history_regression과 gaussian_regression 실행. |
| si_document | 문서 모델, Word·XYZ 작성 | Word 서식은 src/DocxWriter.cpp, 좌표 파일은 src/XyzWriter.cpp. document_regression 실행. |
| si_editor | Qt 화면, 프로젝트 작업 흐름, 문서 조립과 내보내기 | 목록·반응 단계는 src/ReactionWorkspace.cpp, 파일·SPE 가져오기는 src/GaussianImport.cpp, 열기·저장은 src/ProjectWorkspace.cpp, 이력 창은 src/ProjectHistoryDialog.cpp. 표 조립은 src/DocumentAssembly.cpp, 내보내기는 src/DocumentExport.cpp, Figure는 src/FigureSetupDialog.cpp. ui_smoke 실행. |

대상별로 빌드하려면 cmake --build build --config Release --target 대상명을 실행합니다.
검증 실행 파일도 빌드한 뒤 ctest --test-dir build -C Release -R 테스트명 --output-on-failure로 해당 테스트만 실행할 수 있습니다.
UI의 공유 상태와 이벤트 연결은 MainWindow.hpp 및 MainWindow.cpp에서 조율합니다. 기능 작업은 위 진입점에 두고 공유 상태나 공개 데이터 형식을 변경할 때만 의존 모듈을 함께 검증합니다.
