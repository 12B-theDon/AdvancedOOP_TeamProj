# Diagram renderer

`tools/draw_architecture.py`가 모듈 SVG를 생성하고, `RenderDiagrams`가 PNG를 출력합니다. Pillow·Arial 글꼴과 Qt Gui·Svg가 필요합니다. 글꼴 경로는 `DIAGRAM_FONT_DIR`로 지정합니다(Windows: `C:\Windows\Fonts`).

```sh
python3 tools/draw_architecture.py
cmake -S tools/diagram_renderer -B build-refactor/diagram-tools -DCMAKE_PREFIX_PATH="<Qt>"
cmake --build build-refactor/diagram-tools
QT_QPA_PLATFORM=offscreen build-refactor/diagram-tools/RenderDiagrams 2400 docs/diagrams/overview.svg docs/diagrams/algorithm.svg docs/diagrams/engine.svg docs/diagrams/gui.svg docs/diagrams/training.svg docs/diagrams/worker.svg
QT_QPA_PLATFORM=offscreen build-refactor/diagram-tools/RenderDiagrams 1654 docs/diagrams/deepprint_finetuning.svg
QT_QPA_PLATFORM=offscreen build-refactor/diagram-tools/RenderDiagrams 3508 docs/diagrams/deepprint_blocks.svg
```

Windows에서는 환경 변수와 실행파일 경로를 해당 shell·빌드 구성에 맞춥니다. DeepPrint SVG는 직접 편집하며, 모델 변경 시 소스와 대조합니다. PNG 비율·DPI는 SVG의 viewBox·물리 크기를 따릅니다.

[그림 목록](../../docs/diagrams/readme.md) · [스타일](../../docs/diagram_style.md)
