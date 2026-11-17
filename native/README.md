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

## 원자 순서 자동 통일

구조와 반응 구성에서 **원자 순서 자동 통일…**을 누르고 원자 번호의 기준 구조를 선택한 뒤 **자동 대응 계산 → Apply**로 적용합니다. 현재 반응 또는 보관함에 보이는 구조를 처리하며, 다른 반응에서 공유한 구조에도 동일한 원자 번호를 사용합니다.

원소별 거리 지문과 일대일 할당, 회전·이동 정렬로 대응을 찾습니다. 조성이 다르거나 RMSD/개별 원자 편차가 큰 구조는 건너뜁니다. 대칭 원자와 큰 반응 구조 변화는 화학적 대응을 별도로 검토하세요. 결과 행의 툴팁에서 새 번호와 원본 번호의 대응을 확인할 수 있습니다.

원본 좌표값과 Gaussian 파일은 보존합니다. 프로젝트에 저장된 대응표가 3D 뷰, 스핀, 기하, Figure, DOCX/XYZ에 적용되고, 원본 파일이 없어도 스냅샷으로 복원됩니다. 원본 좌표가 바뀌면 대응표를 적용하지 않고 경고합니다. **현재 목록 원본 순서 복원**으로 되돌릴 수 있습니다. 공통 기하·스핀 ID는 통일된 번호를 사용하고 구조별 스핀 선택과 Figure의 표시 수소는 원자 실체를 유지합니다.


## 하위 폴더별 기준점과 문서 서식

- **하위 폴더별 반응 만들기**는 가져온 구조 중 아직 반응에 배정되지 않은 구조를 원본 폴더별로 묶습니다. 반응 설정의 **이 반응의 기준 구조** 또는 **이 반응의 수동 기준값 (Hartree)**으로 각 반응의 기준점을 독립적으로 지정합니다. 새 반응은 독립 기준을 기본으로 사용하며, 기존 프로젝트는 **전체 기준 방식 따르기**를 유지합니다. 반응 밖의 구조도 기준으로 선택할 수 있습니다.
- **문서와 출력 → Table/Figure 캡션 위치**에서 위·아래를 선택합니다. 기본은 Table 위, Figure 아래이며 프로젝트에 저장됩니다. 미리보기, 표 복사, DOCX에 동일하게 적용됩니다. 기하 표의 원자 연결은 `C(1) – H(2)`처럼 en-dash 앞뒤를 한 칸씩 띄웁니다.

## 3D 뷰어 조작

- 왼쪽 또는 오른쪽 드래그: 트랙볼 회전. Ctrl+오른쪽, 가운데 또는 Shift 드래그: 이동. 휠 또는 Ctrl+Shift+오른쪽 드래그: 확대·축소. Alt 드래그: 화면축 회전.
- **보기 · 측정** 메뉴에서 화면 맞춤, 선택 원자 중심 회전, XY/YZ/XZ 보기, 측정, 측정값 복사, 원자 번호를 바로 사용합니다. Figure 편집 창에서도 같은 메뉴를 제공합니다.
- Space/F: 화면 맞춤, 5: 선택 원자 중심 회전(선택이 없으면 전체 맞춤), 1/2/3: XY/YZ/XZ, Home: 처음 보기, Esc/오른쪽 클릭: 선택 해제, Ctrl+C: 측정값 복사. 빈 곳 더블 클릭으로도 화면을 맞춥니다.
- 거리·각도·이면각 측정은 원자 2/3/4개를 순서대로 클릭합니다. 회전 중심과 카메라 방향은 Figure 설정과 함께 저장됩니다. 조작은 Gaussian 좌표를 변경하지 않습니다.
