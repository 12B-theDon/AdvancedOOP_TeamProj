"""One-time upstream checkpoint -> trainable LibTorch TorchScript conversion."""
# DeepPrint upstream: LGPL-3.0; InceptionV4: BSD-3-Clause. See third_party/deepprint.
# PyTorch: https://github.com/pytorch/pytorch (BSD-3-Clause).
# torchvision: https://github.com/pytorch/vision (BSD-3-Clause).
# Python is used only for checkpoint conversion.
import argparse
import hashlib
import json
import re
from pathlib import Path
import sys

import torch
from torch import nn
from torch.nn import functional as F

ROOT = Path(__file__).resolve().parents[1]
def setting(header, name):
    text = (ROOT / "src/Algorithm" / header).read_text()
    value = re.search(r"\b" + name + r"\s*=\s*([^;]+);", text).group(1).strip()
    return json.loads(value)


INPUT_SIZE = setting("encoderConfig.h", "InputSize")
EMBEDDING_SIZE = setting("encoderConfig.h", "EmbeddingSize")
FEATURE_SIZE = setting("encoderConfig.h", "FeatureSize")
TEXTURE_FEATURES = setting("encoderConfig.h", "TextureFeatureSize")
BRANCH_SIZE = setting("encoderConfig.h", "BranchEmbeddingSize")
GABOR_WIDTH = setting("encoderConfig.h", "GaborRidgeWidth")
ARCHITECTURE = setting("encoderConfig.h", "Architecture")
PREPROCESSING = setting("encoderConfig.h", "Preprocessing")
SOURCE_COMMIT = setting("encoderConfig.h", "UpstreamCommit")

sys.path.insert(0, str(ROOT / "third_party/deepprint"))
from flx.models.deep_print_arch import DeepPrint_TexMinu
from flx.image_processing.binarization import _FingerprintBinarizer


class Encoder(nn.Module):
    def __init__(self, source):
        super().__init__()
        self.stem = source.stem
        self.texture_blocks = nn.Sequential(
            source.texture_branch._0_block,
            source.texture_branch._1_block,
            source.texture_branch._2_block,
            source.texture_branch._3_avg_pool2d,
            nn.Flatten(),
        )
        self.minutia_blocks = nn.Sequential(
            source.minutia_stem, source.minutia_embedding._0_block,
            source.minutia_embedding._1_max_pool2d, nn.Flatten(),
        )
        self.texture_projection = source.texture_branch._6_linear
        self.minutia_projection = source.minutia_embedding._4_linear
        self.binarizer = _FingerprintBinarizer(GABOR_WIDTH)

    def preprocess(self, image):
        return self.binarizer.binarize(image.squeeze(0)).unsqueeze(0)

    def features(self, image):
        stem = self.stem(image)
        return torch.cat((self.texture_blocks(stem), self.minutia_blocks(stem)), dim=1)

    def embed_features(self, features):
        texture = F.normalize(self.texture_projection(features[:, :TEXTURE_FEATURES]), dim=1)
        minutia = F.normalize(self.minutia_projection(features[:, TEXTURE_FEATURES:]), dim=1)
        return F.normalize(torch.cat((texture, minutia), dim=1), dim=1)

    def forward(self, image):
        return self.embed_features(self.features(image))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("checkpoint", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError(args.output)
    torch.set_num_threads(setting("encoderConfig.h", "Threads"))
    torch.manual_seed(setting("trainConfig.h", "Seed"))
    checkpoint = torch.load(args.checkpoint, map_location="cpu", weights_only=True)
    state = checkpoint["model_state_dict"]
    classes, texture_dims = state["texture_logits.0.weight"].shape
    minutia_dims = state["minutia_logits.0.weight"].shape[1]
    if (texture_dims, minutia_dims) != (BRANCH_SIZE, BRANCH_SIZE):
        raise ValueError("Expected the published DeepPrint_TexMinu_512 checkpoint")
    source = DeepPrint_TexMinu(classes, texture_dims, minutia_dims).eval()
    source.load_state_dict(state, strict=True)
    model = Encoder(source).eval()
    image = torch.rand(2, 1, INPUT_SIZE, INPUT_SIZE)
    preprocessed = model.preprocess(image[:1])
    with torch.no_grad():
        original = source(preprocessed.repeat(2, 1, 1, 1))
        expected = F.normalize(torch.cat((original.texture_embeddings, original.minutia_embeddings), 1), dim=1)
        torch.testing.assert_close(model(preprocessed.repeat(2, 1, 1, 1)), expected)
    traced = torch.jit.trace_module(model, {
        "forward": image,
        "features": image,
        "embed_features": torch.rand(2, FEATURE_SIZE),
        "preprocess": image[:1],
    }, check_trace=False)
    maximum_error = 0.0
    for batch in (1, 2, 4):
        sample = torch.rand(batch, 1, INPUT_SIZE, INPUT_SIZE)
        with torch.no_grad():
            actual, expected = traced(sample), model(sample)
        torch.testing.assert_close(actual, expected, atol=setting("encoderConfig.h", "ExportAtol"), rtol=setting("encoderConfig.h", "ExportRtol"))
        maximum_error = max(maximum_error, (actual - expected).abs().max().item())
    for sample in (image[:1], torch.zeros_like(image[:1]), torch.ones_like(image[:1])):
        torch.testing.assert_close(traced.preprocess(sample), model.preprocess(sample))
    traced.zero_grad()
    traced.embed_features(torch.rand(2, FEATURE_SIZE)).sum().backward()
    assert traced.texture_projection.weight.grad is not None
    args.output.parent.mkdir(parents=True, exist_ok=True)
    metadata = {
        "format": "deepprint-torchscript-v1",
        "model_id": "pretrained",
        "architecture": ARCHITECTURE,
        "preprocessing": PREPROCESSING,
        "embedding_size": EMBEDDING_SIZE,
        "source_commit": SOURCE_COMMIT,
        "checkpoint_sha256": hashlib.file_digest(args.checkpoint.open("rb"), "sha256").hexdigest(),
        "checkpoint_url": "https://drive.google.com/file/d/1R9v73hFBuy0iihz7v8eDlBcGrMDmQ9TH/view",
        "source_license": "LGPL-3.0; InceptionV4 BSD-3-Clause; no separate checkpoint license supplied",
        "torch_version": torch.__version__,
        "strict_state_dict": True,
        "upstream_classes": classes,
        "export_max_abs_error": maximum_error,
        "data_scope": "published pretrained checkpoint; no FVC fine-tuning",
    }
    traced.save(str(args.output), _extra_files={"metadata.json": json.dumps(metadata)})
    reloaded = torch.jit.load(str(args.output))
    torch.testing.assert_close(reloaded(image[:1]), traced(image[:1]))
    args.output.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
