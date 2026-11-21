# Diagram style

기존 SVG에서 확인한 색·글꼴·도형 규칙을 사용합니다. 크기와 좌표는 SVG viewBox 기준입니다.

## 소스 구조도

- 흰 배경, 폭 940. 열 중심 150/470/790(기존 140/470/800), overview는 process 그룹에 맞춰 150/460/780. 카드 간격 최소 28, 바깥 여백 최소 12.
- 직각 사각형 카드: 테두리 2, header 높이 32.5, 카드 높이 `38 + 18×본문 행 수`. 글자 폭에 여백을 더해 카드 폭 결정.
- Arial: 제목 30 bold·밑줄, 흰 header 18 bold, 검정 본문 14, 연결선 라벨 12. 본문 행 간격 18. 그룹 테두리는 1.
- 호출·데이터는 `#4f5b65` 실선 2, 직접 include는 `#81909b` 점선 5/6. 경계에서 연결하고 최대 한 번 꺾음. 화살촉 8×6.6, 라벨 여백 6. 요청·응답은 평행한 별도 선.

| 구분 | 본문 / header·테두리 |
|---|---|
| Algorithm·Training | `#eaf1ff` / `#2864b4` |
| Engine | `#e8f6ec` / `#21834d` |
| GUI·Report | `#f1e8fc` / `#7c4db5` |
| Shared | `#f4f6f8` / `#73818d` |
| Entry·Worker | `#fff1e2` / `#c96818` |

## DeepPrint

AppleGothic 10 pt, 연산 8 pt, 차원 6 pt. 본문 `#173452`, 보조 글자 `#52677c`. 모듈은 반경 5·선 0.8, panel은 반경 7·`#f9fbfd/#cbd6e1`입니다.

Conv `#edf4fd/#5186be`, Pool `#c8d3e2/#9db3cb`, Inception A `#fae3ea/#ad3e64`, B `#f3eef9/#7c4e9b`, C `#eaf6f2/#19766d`, Reduction B `#f7e9bf/#96722b`, 학습 모듈 `#fff2df/#df8b21`을 사용합니다. Frozen·trainable 표시는 벡터 눈송이·불꽃입니다.

Fine-tuning의 두 branch 중심은 197/398, 합류는 297.5입니다. 내부 병렬 셀은 18·간격 3, 반복 카드는 1.2/−0.55 offset으로 쌓습니다. 블록 해부도는 A4 가로 3×2 panel, 여백 12·간격 8·높이 268, 연결선 1.25입니다.

## 출력

소스 구조도는 SVG 폭 297 mm·PNG 폭 2400 px입니다. Fine-tuning은 595.276×790 pt → 1654×2195 px(약 200 dpi), 블록은 841.89×595.276 pt → 3508×2480 px(300 dpi, 기존 렌더 2481 px)입니다. 종횡비와 DPI 메타데이터를 유지하고 글자·화살표 겹침을 확인합니다.
