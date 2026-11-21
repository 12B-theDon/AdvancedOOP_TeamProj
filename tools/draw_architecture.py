"""Draw source-checked architecture SVGs; render with render_diagrams.cpp.

Requires Pillow only for font measurements. SVGs remain directly editable.
"""
from dataclasses import dataclass
from html import escape
from pathlib import Path
import hashlib
import json
import re
import os

from PIL import ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/diagrams'
FONT = Path(os.environ.get('DIAGRAM_FONT_DIR', '/System/Library/Fonts/Supplemental'))
PALETTE = {
    'algorithm': ('#eaf1ff', '#2864b4'),
    'engine': ('#e8f6ec', '#21834d'),
    'gui': ('#f1e8fc', '#7c4db5'),
    'shared': ('#f4f6f8', '#73818d'),
    'entry': ('#fff1e2', '#c96818'),
}
FONTS = {}

def measure(text, size, bold=False):
    key = (size, bold)
    if key not in FONTS:
        candidates = ['Arial Bold.ttf', 'arialbd.ttf'] if bold else ['Arial.ttf', 'arial.ttf']
        file = next((FONT / name for name in candidates if (FONT / name).exists()), None)
        if file is None:
            raise RuntimeError('Set DIAGRAM_FONT_DIR to a folder containing Arial regular/bold fonts.')
        FONTS[key] = ImageFont.truetype(str(file), size * 4)
    return FONTS[key].getlength(text) / 4

@dataclass
class Card:
    key: str
    title: str
    x: float
    y: float
    width: float
    height: float
    sources: tuple
    def port(self, side, offset=0):
        return {
            'N': (self.x + self.width / 2 + offset, self.y),
            'S': (self.x + self.width / 2 + offset, self.y + self.height),
            'W': (self.x, self.y + self.height / 2 + offset),
            'E': (self.x + self.width, self.y + self.height / 2 + offset),
        }[side]

class Diagram:
    def __init__(self, name, title, height):
        self.name, self.height = name, height
        self.cards, self.shapes, self.edges, self.texts = {}, [], [], []
        self.sources = set()
        self.title = title
        self.text(470, 43, title, 30, bold=True)
        self.shapes.append('<line x1="250" y1="64" x2="690" y2="64" stroke="#73818d"/>')
    def text(self, x, y, value, size=14, bold=False, color='#111111', anchor='middle'):
        width = measure(value, size, bold)
        assert 8 <= x - (width / 2 if anchor == 'middle' else 0), (self.name, value, 'left margin')
        assert x + (width / 2 if anchor == 'middle' else width) <= 932, (self.name, value, 'right margin')
        assert 10 <= y <= self.height - 6, (self.name, value, 'vertical margin')
        self.texts.append(f'<text x="{x}" y="{y}" font-family="Arial" font-size="{size}" font-weight="{700 if bold else 400}" text-anchor="{anchor}" fill="{color}">{escape(value)}</text>')
    def group(self, x, y, width, height, title, color='#73818d'):
        self.shapes.append(f'<rect x="{x}" y="{y}" width="{width}" height="{height}" fill="#fbfcfe" stroke="{color}" stroke-width="1"/>')
        self.text(x + 12, y + 23, title, 16, bold=True, color=color, anchor='start')
    def card(self, key, title, x, y, lines, kind='shared', source=None, width=260, checks=()):
        assert key not in self.cards
        height = 38 + 18 * len(lines)
        assert measure(title, 18, True) <= width - 16, (self.name, title, 'title too wide')
        for line in lines:
            assert measure(line, 14) <= width - 16, (self.name, title, line, 'body too wide')
        sources = (source,) if isinstance(source, str) else tuple(source or ())
        self.sources.update(sources)
        for item in sources:
            assert (ROOT / item).is_file(), (self.name, item)
        if checks:
            content = '\n'.join((ROOT / item).read_text(encoding='utf-8') for item in sources)
            for token in checks:
                assert token in content, (self.name, title, token)
        node = Card(key, title, x, y, width, height, sources)
        for previous in self.cards.values():
            assert x + width <= previous.x or previous.x + previous.width <= x or y + height <= previous.y or previous.y + previous.height <= y, (self.name, key, previous.key, 'cards overlap')
        self.cards[key] = node
        fill, stroke = PALETTE[kind]
        self.shapes.append(f'<g data-node="{key}" data-source="{escape(";".join(sources))}"><rect x="{x}" y="{y}" width="{width}" height="{height}" fill="{fill}" stroke="{stroke}" stroke-width="2"/><rect x="{x}" y="{y}" width="{width}" height="32.5" fill="{stroke}"/></g>')
        self.text(x + width / 2, y + 23.5, title, 18, True, '#ffffff')
        for index, line in enumerate(lines):
            self.text(x + width / 2, y + 48.5 + index * 18, line)
        return node
    def link(self, source, target, sides=('E', 'W'), offsets=(0, 0), label='', at=None, include=False):
        a, b = self.cards[source], self.cards[target]
        if include:
            verified = False
            for path in a.sources:
                text = (ROOT / path).read_text(encoding='utf-8')
                for name in re.findall(r'#include\s+"([^"]+)"', text):
                    local = (ROOT / path).parent / name
                    candidates = [local, *(ROOT / folder / name for folder in ['src/Algorithm', 'src/Engine', 'src/Training', 'src/GUI'])]
                    if any(candidate.resolve() == (ROOT / dest).resolve() for candidate in candidates for dest in b.sources):
                        verified = True
            assert verified, (self.name, source, target, 'not a direct include')
        start, end = a.port(sides[0], offsets[0]), b.port(sides[1], offsets[1])
        points = [start]
        if start[0] != end[0] and start[1] != end[1]:
            points.append((end[0], start[1]) if sides[0] in 'EW' else (start[0], end[1]))
        points.append(end)
        # Route checking: connectors may touch their endpoints but cross no card interior.
        for p, q in zip(points, points[1:]):
            for other in self.cards.values():
                if other.key in (source, target):
                    continue
                if p[0] == q[0]:
                    crosses = other.x < p[0] < other.x + other.width and max(min(p[1], q[1]), other.y) < min(max(p[1], q[1]), other.y + other.height)
                else:
                    crosses = other.y < p[1] < other.y + other.height and max(min(p[0], q[0]), other.x) < min(max(p[0], q[0]), other.x + other.width)
                assert not crosses, (self.name, source, target, 'edge crosses', other.key)
        color = '#81909b' if include else '#4f5b65'
        dash = ' stroke-dasharray="5 6"' if include else ''
        geometry = ' '.join(f'{x},{y}' for x, y in points)
        previous = points[-2]
        dx, dy = end[0] - previous[0], end[1] - previous[1]
        distance = (dx * dx + dy * dy) ** .5
        ux, uy = dx / distance, dy / distance
        base = end[0] - 8 * ux, end[1] - 8 * uy
        triangle = f'{end[0]},{end[1]} {base[0] - 3.3 * uy},{base[1] + 3.3 * ux} {base[0] + 3.3 * uy},{base[1] - 3.3 * ux}'
        self.edges.append(f'<g data-from="{source}" data-to="{target}" data-relation="{"include" if include else "flow"}"><polyline points="{geometry}" fill="none" stroke="{color}" stroke-width="2"{dash}/><polygon points="{triangle}" fill="{color}"/></g>')
        if label:
            assert at is not None
            self.text(*at, label, 12, color='#52606d')
    def save(self, legend=True):
        if legend:
            self.text(470, self.height - 20, 'Solid: calls / data flow     Dashed: selected direct #include links', 12, color='#52606d')
        svg = f'<svg xmlns="http://www.w3.org/2000/svg" width="297mm" height="{self.height / 940 * 297:.4f}mm" viewBox="0 0 940 {self.height}" role="img"><title>{escape(self.title)}</title><desc>Current project implementation; editable vector source. Generated from verified source paths and selected direct includes.</desc><rect width="940" height="{self.height}" fill="white"/>\n' + '\n'.join(self.shapes + self.edges + self.texts) + '\n</svg>\n'
        (OUT / (self.name + '.svg')).write_text(svg, encoding='utf-8')
        return self.sources

def overview():
    d = Diagram('overview', 'Runtime boundaries and training', 950)
    d.group(20, 95, 580, 282, 'Evaluator process · Qt only', '#21834d')
    d.group(630, 95, 290, 805, 'Worker process × N', '#c96818')
    d.group(645, 570, 260, 315, 'Core DLL', '#2864b4')
    d.group(20, 735, 580, 170, 'Training process', '#2864b4')
    d.card('gui', 'GUI.cpp', 40, 140, ['Window / seven steps', 'RunWithProgress()', 'Results / threshold 0.5'], 'gui', 'src/GUI/GUI.cpp', 220, ['RunWithProgress'])
    d.card('engine', 'Engine.cpp', 340, 140, ['PlanComparisons()', 'EnrollImages() / Execute()', 'opaque template cache'], 'engine', 'src/Engine/Engine.cpp', 240)
    d.card('adapter', 'kisaDll.cpp', 340, 270, ['KisaDllAlgorithm', 'process pool / deadlines', 'Cancel() / ResetCancel()'], 'engine', 'src/Engine/kisaDll.cpp', 240)
    d.card('report', 'report.cpp', 40, 270, ['CalculateMetrics()', 'WriteGraphs() / Save()', 'selected report items'], 'gui', 'src/GUI/report.cpp', 220)
    d.card('worker', 'worker.cpp', 660, 270, ['FingerprintWorker.exe', 'LoadLibraryExW()', 'isolated KISA calls'], 'entry', 'src/Worker/worker.cpp', 240)
    d.card('public', 'BioApi.cpp', 660, 430, ['FingerprintAlgorithm.dll', 'three public KISA exports', 'lazy Core forwarding'], 'algorithm', 'src/Kisa/BioApi.cpp', 240)
    d.card('core', 'core.cpp', 660, 610, ['FingerprintAlgorithmCore.dll', 'CoreEnroll() / CoreAttempt()', 'CoreCompare()', 'private bundle / minimum score'], 'algorithm', 'src/Kisa/core.cpp', 240)
    d.card('encoder', 'encoder.cpp', 660, 775, ['FingerprintEncoder', 'Preprocess() / CreateTemplate()', 'Compare()', 'LibTorch + Qt Core'], 'algorithm', 'src/Algorithm/encoder.cpp', 240)
    d.card('outputs', 'Evaluation outputs', 40, 440, ['CSV + opaque templates', 'HTML / Word-compatible RTF', 'separate vector SVG graphs', 'original pair order'], 'shared', width=220)
    d.card('train', 'train.cpp', 40, 775, ['FingerprintTrain', 'independent fine-tuning', 'training / native validation', 'exports final archives'], 'algorithm', 'src/Training/train.cpp', 220)
    d.card('weights', 'Four .bin archives', 340, 775, ['fvc2000_db3.bin', 'fvc2002_db3.bin', 'fvc2004_db1.bin / combined.bin', 'beside algorithm DLLs'], width=240)
    d.link('gui', 'engine', label='requests', at=(300, 173))
    d.link('engine', 'adapter', ('S', 'N'))
    d.link('gui', 'report', ('S', 'N'))
    d.link('report', 'outputs', ('S', 'N'), label='save', at=(169, 407))
    d.link('adapter', 'worker', offsets=(-16, -16), label='request', at=(620, 291))
    d.link('worker', 'adapter', ('W', 'E'), (16, 16), 'reply', (620, 351))
    d.link('worker', 'public', ('S', 'N'), label='cdecl', at=(809, 407))
    d.link('public', 'core', ('S', 'N'))
    d.link('core', 'encoder', ('S', 'N'), label='encode / compare', at=(800, 752))
    d.link('core', 'weights', ('W', 'N'), label='load once', at=(535, 652))
    d.link('train', 'weights', label='export', at=(300, 823))
    d.text(40, 585, 'Startup: Git LFS -> fallback train -> package -> GUI.', 14, anchor='start')
    d.sources.update(['src/GUI/startup.h', 'src/GUI/startup.cpp', 'tools/setup_windows.ps1'])
    d.text(40, 612, 'Evaluator: original gray8 + opaque template bytes.', 14, anchor='start')
    d.text(40, 638, 'Windows runtime; DLLs and weights share one folder.', 14, anchor='start')
    return d.save(False)

def algorithm():
    d = Diagram('algorithm', 'Algorithm · public contract and implementation', 680)
    d.card('abi', 'BioApi.h', 20, 110, ['KISA_Enroll()', 'KISA_Attempt()', 'KISA_Compare()'], source='src/Public/BioApi.h')
    d.card('fvc', 'FvcSpec.h', 340, 110, ['1: FVC2000_DB3_B · 448×478', '2: FVC2002_DB3_B · 300×300', '3: FVC2004_DB1_B · 640×480'], source='src/Public/FvcSpec.h')
    d.card('config', 'encoderConfig.h', 660, 110, ['InputSize = 299', 'EmbeddingSize = 512', 'TemplateSize = 30'], 'algorithm', 'src/Algorithm/encoderConfig.h')
    d.card('public', 'BioApi.cpp', 20, 300, ['KISA_Enroll() / KISA_Attempt()', 'KISA_Compare()', 'LoadCore()', 'module-relative Core DLL'], 'algorithm', 'src/Kisa/BioApi.cpp')
    d.card('core', 'core.cpp', 340, 300, ['ModuleFolder() / State::Load()', 'Extract() / Encode() / Decode()', 'CoreEnroll() / CoreAttempt()', 'min: matching-DB / combined'], 'algorithm', 'src/Kisa/core.cpp')
    d.card('encoder', 'encoder.cpp', 660, 300, ['LoadModel()', 'Preprocess() / CreateTemplate()', 'Compare()', 'probability30 / cosine'], 'algorithm', 'src/Algorithm/encoder.cpp')
    d.card('image', 'Image.h', 20, 530, ['fingerprint::Image', 'width / height / gray8 pixels', 'database_id'], source='src/Public/Image.h')
    d.card('api', 'Algorithm.h', 340, 530, ['algorithm::Algorithm', 'PreparedImage / model metadata', 'native training / encoder API'], 'algorithm', 'src/Algorithm/Algorithm.h')
    d.card('encoderh', 'encoder.h', 660, 530, ['FingerprintEncoder', 'PImpl owns loaded modules', 'copying deleted'], 'algorithm', 'src/Algorithm/encoder.h')
    d.link('public', 'abi', ('N', 'S'), include=True)
    d.link('core', 'fvc', ('N', 'S'), include=True)
    d.link('encoder', 'config', ('N', 'S'), include=True)
    d.link('public', 'core', label='forward', at=(310, 343))
    d.link('core', 'encoder', label='calls', at=(630, 343))
    d.link('encoder', 'encoderh', ('S', 'N'), include=True)
    d.link('encoderh', 'api', ('W', 'E'), include=True)
    d.link('api', 'image', ('W', 'E'), include=True)
    return d.save()

def engine():
    d = Diagram('engine', 'Engine · generic evaluation and DLL adapter', 710)
    d.card('config', 'engineConfig.h', 20, 110, ['workers 1–16 / seed 42', 'call 30 s / first call ≥120 s', 'startup 30 s / threshold 0.5'], 'engine', 'src/Engine/engineConfig.h')
    d.card('engineh', 'Engine.h', 340, 110, ['ImageInfo / ComparisonPair', 'EnrollmentTemplates', 'SimilarityResult / EvaluationRun'], 'engine', 'src/Engine/Engine.h')
    d.card('contract', 'Algorithm.h', 660, 110, ['engine::Algorithm interface', 'raw image / opaque templates', 'Extract() / Compare() / Cancel()'], 'engine', 'src/Engine/Algorithm.h')
    d.card('gui', 'GUI.cpp', 20, 300, ['CreateEnrollmentTemplates()', 'RunSelectedComparisons()', 'owns backend lifetime', 'background work joins before release'], 'gui', 'src/GUI/GUI.cpp')
    d.card('engine', 'Engine.cpp', 340, 300, ['PlanComparisons()', 'EnrollImages() / ParallelFor()', 'Execute(): cache distinct attempts', 'original pair order / Stop discards'], 'engine', 'src/Engine/Engine.cpp')
    d.card('adapter', 'kisaDll.cpp', 660, 300, ['KisaDllAlgorithm::State', 'restartable worker process pool', 'deadlines / Cancel() / ResetCancel()', 'finite scores / bounded templates'], 'engine', 'src/Engine/kisaDll.cpp')
    d.card('protocol', 'Protocol.h', 20, 550, ['shared by host and worker', 'version 2 / fixed-width layout', '24-byte request / 296-byte reply'], source='src/Worker/Protocol.h')
    d.card('raw', 'Raw evaluation files', 340, 550, ['SaveRawResults()', 'actual template byte lengths', 'separate attempt / Compare time'], 'shared')
    d.card('adapterh', 'kisaDll.h', 660, 550, ['KisaDllAlgorithm final', 'DllOptions', 'owns processes; no encoder link'], 'engine', 'src/Engine/kisaDll.h')
    d.link('engineh', 'config', ('W', 'E'), include=True)
    d.link('engineh', 'contract', include=True)
    d.link('engine', 'engineh', ('N', 'S'), include=True)
    d.link('gui', 'engine', label='images / pairs', at=(310, 343))
    d.link('engine', 'adapter', label='virtual call', at=(630, 343))
    d.link('adapter', 'adapterh', ('S', 'N'), include=True)
    d.link('engine', 'raw', ('S', 'N'), label='export', at=(502, 500))
    return d.save()

def gui():
    d = Diagram('gui', 'GUI · ownership, background work and reports', 710)
    d.card('reporth', 'report.h', 20, 110, ['CalculateMetrics()', 'GraphFiles() / WriteGraphs()', 'Save()'], 'gui', 'src/GUI/report.h')
    d.card('guih', 'GUI.h', 340, 110, ['Window / seven-page QWizard', 'owns Engine + KisaDllAlgorithm', 'cached templates and results'], 'gui', 'src/GUI/GUI.h')
    d.card('config', 'guiConfig.h', 660, 110, ['page titles / controls', 'report metric choices', 'threshold display / colors'], 'gui', 'src/GUI/guiConfig.h')
    d.card('report', 'report.cpp', 20, 300, ['assignment metrics / failure counts', 'EER midpoint estimate + bounds', 'selected HTML / editable RTF', 'separate SVG graph exports'], 'gui', 'src/GUI/report.cpp')
    d.card('gui', 'GUI.cpp', 340, 300, ['RunWithProgress() / Stop', 'ApplyThreshold() / Snapshot()', 'AlgorithmUnchanged()', 'SaveResults()'], 'gui', 'src/GUI/GUI.cpp')
    d.card('engine', 'Engine.h', 660, 300, ['PlanComparisons()', 'EnrollImages() / Execute()', 'Cancel() / progress counters', 'borrowed algorithm interface'], 'engine', 'src/Engine/Engine.h')
    d.card('outputs', 'Report outputs', 20, 550, ['HTML / Word-compatible RTF', 'SVG graphs / all raw CSV', 'selected metrics in reports'], 'shared')
    d.card('main', 'main.cpp / startup', 340, 550, ['QApplication / startup.cpp', 'Windows: prepare missing bundle', 'then gui::Window'], 'entry', ('src/main.cpp', 'src/GUI/startup.cpp', 'src/GUI/startup.h', 'tools/setup_windows.ps1'))
    d.card('backend', 'kisaDll.h', 660, 550, ['algorithm_ owned by Window', 'replacement clears cached results', 'selected DLL + bundle signature'], 'engine', 'src/Engine/kisaDll.h')
    d.link('report', 'reporth', ('N', 'S'), include=True)
    d.link('gui', 'guih', ('N', 'S'), include=True)
    d.link('guih', 'config', include=True)
    d.link('gui', 'report', ('W', 'E'), (-16, -16), 'snapshot', (310, 332))
    d.link('report', 'gui', ('E', 'W'), (16, 16), 'metrics', (310, 388))
    d.link('gui', 'engine', label='calls', at=(630, 343))
    d.link('report', 'outputs', ('S', 'N'), label='save', at=(181, 495))
    d.link('main', 'gui', ('N', 'S'), label='construct', at=(505, 495))
    return d.save()

def training():
    d = Diagram('training', 'Training · augmentation, fine-tuning and validation', 710)
    d.card('augmentcfg', 'augmentConfig.h', 20, 110, ['12 copies per original / seed 42', 'FVC2004 crop before augmentation', 'rotation / scale / noise / brightness'], 'algorithm', 'src/Training/augmentConfig.h')
    d.card('traincfg', 'trainConfig.h', 340, 110, ['Adam / learning rate 0.0001', 'batch 32 / 40 epochs / seed 42', 'two projections + cosine classifier'], 'algorithm', 'src/Training/trainConfig.h')
    d.card('evalcfg', 'evaluationConfig.h', 660, 110, ['four independent model IDs', 'validation FAR target 1%', 'calibration precision 4 decimals'], 'algorithm', 'src/Training/evaluationConfig.h')
    d.card('augment', 'augment.cpp', 20, 300, ['PrepareAugmentations()', 'original files remain unchanged', 'split original captures first', 'training 1–6 / validation 7–8'], 'algorithm', 'src/Training/augment.cpp')
    d.card('train', 'train.cpp', 340, 300, ['PrepareTrainingData()', 'verified frozen-feature cache', 'RunTraining()', 'independent pretrained initialization'], 'algorithm', 'src/Training/train.cpp')
    d.card('validation', 'validation.cpp', 660, 300, ['EvaluateTrainingRun()', 'ValidationEngine native audit', 'frozen weights / buffers check', 'calibration + validation scores'], 'algorithm', ('src/Training/validation.cpp', 'src/Training/ValidationEngine.cpp', 'src/Training/ValidationEngine.h'))
    d.card('augmented', 'data/augmented', 20, 550, ['offline training copies', 'manifest / source SHA-256', 'validation uses originals only'])
    d.card('weights', 'Four saved models', 340, 550, ['per-DB models + combined', 'retained Softmax30 classifier', 'final-epoch TorchScript .bin files'])
    d.card('validationout', 'Validation outputs', 660, 550, ['validation CSV / HTML / JSON', 'native / frozen-backbone audit', 'calibration remains training output'])
    d.link('augment', 'augmentcfg', ('N', 'S'), include=True)
    d.link('train', 'traincfg', ('N', 'S'), include=True)
    d.link('train', 'augment', ('W', 'E'), (-16, -16), 'prepare', (310, 332))
    d.link('augment', 'train', ('E', 'W'), (16, 16), 'samples', (310, 388))
    d.link('train', 'validation', label='verify', at=(630, 343))
    d.link('augment', 'augmented', ('S', 'N'), label='write', at=(180, 500))
    d.link('train', 'weights', ('S', 'N'), label='export', at=(500, 500))
    d.link('validation', 'validationout', ('S', 'N'), label='write', at=(821, 500))
    return d.save()

def worker():
    d = Diagram('worker', 'Windows worker · IPC and crash isolation', 730)
    d.card('options', 'kisaDll.h', 20, 110, ['DllOptions / KisaDllAlgorithm', 'configurable workers and deadlines', 'Cancel() / ResetCancel()'], 'engine', 'src/Engine/kisaDll.h')
    d.card('protocol', 'Protocol.h', 340, 110, ['version 2 / little-endian Windows', 'Request: operation + byte lengths', 'Response: result / error / duration'], source='src/Worker/Protocol.h')
    d.card('abi', 'BioApi.h', 660, 110, ['cdecl / C linkage / success 1 or 0', 'KISA_Enroll() / KISA_Attempt()', 'KISA_Compare()'], source='src/Public/BioApi.h')
    d.card('host', 'kisaDll.cpp', 20, 300, ['State::Call() / Start() / Stop()', 'owns pipe / process / job handles', 'overlapped writes / read polling', 'deadline or Stop terminates worker'], 'engine', 'src/Engine/kisaDll.cpp')
    d.card('worker', 'worker.cpp', 340, 300, ['LoadLibraryExW() / GetProcAddress()', 'Read() → Invoke() → Write()', 'SEH / output guards / ABI checks', 'dedicated IPC; DLL stdout isolated'], 'entry', 'src/Worker/worker.cpp')
    d.card('dll', 'Selected algorithm DLL', 660, 300, ['KISA_Enroll(iType, gray8, ...)', 'KISA_Attempt(iType, gray8, ...)', 'KISA_Compare(enroll, attempt, ...)', 'templates remain opaque to host'], 'algorithm')
    d.card('errors', 'Failure categories', 20, 550, ['FAIL · DLL error code / message', 'TIMEOUT · CRASH · MISMATCH', 'CANCELLED · stopped run discarded'])
    d.card('lifecycle', 'Worker lifecycle', 340, 550, ['job kills worker on handle close', 'fault → fresh process on next call', 'first call allows ≥120 seconds'])
    d.card('buffers', 'Buffer contract', 660, 550, ['template length >0 and <1,000,000', 'error message: 256 bytes', 'save and replay actual byte lengths'])
    d.link('host', 'options', ('N', 'S'), include=True)
    d.link('worker', 'protocol', ('N', 'S'), include=True)
    d.link('host', 'worker', offsets=(-16, -16), label='request', at=(310, 332))
    d.link('worker', 'host', ('W', 'E'), (16, 16), 'reply', (310, 388))
    d.link('worker', 'dll', offsets=(-16, -16), label='cdecl', at=(630, 332))
    d.link('dll', 'worker', ('W', 'E'), (16, 16), 'outputs', (630, 388))
    d.link('host', 'errors', ('S', 'N'), label='record', at=(183, 500))
    d.link('worker', 'lifecycle', ('S', 'N'), label='fault / next call', at=(525, 500))
    return d.save()

if __name__ == '__main__':
    OUT.mkdir(parents=True, exist_ok=True)
    sources = set()
    for draw in [overview, algorithm, engine, gui, training, worker]:
        sources.update(draw())
    sources.update(['CMakeLists.txt', 'src/Kisa/FingerprintAlgorithm.def', 'tools/prepare_deepprint.py',
        'third_party/deepprint/flx/models/InceptionV4.py', 'third_party/deepprint/flx/models/deep_print_arch.py'])
    manifest = {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in sorted(sources)}
    (OUT / 'source_manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f'Generated six architecture SVGs; verified {len(sources)} source paths, text widths, card bounds and include edges.')
