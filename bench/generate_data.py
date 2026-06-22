#!/usr/bin/env python3
"""Generate benchmark datasets: various dims, sizes, and optional real embeddings."""

import argparse
import csv
import os
import random
import struct
import sys

SEED = 42
DATA_DIR = os.path.join(os.path.dirname(__file__), "data")

DATASETS = [
    (128, 1000),
    (128, 10000),
    (384, 1000),
    (384, 10000),
    (384, 100000),
    (768, 1000),
    (768, 10000),
    (1536, 1000),
    (1536, 10000),
]


def random_vec(dim, rng):
    return [round(rng.uniform(-1.0, 1.0), 6) for _ in range(dim)]


def clustered_vec(dim, rng, cluster_center=None):
    if cluster_center is None:
        cluster_center = [rng.uniform(-1, 1) for _ in range(dim)]
    return [round(c + rng.gauss(0, 0.1), 6) for c in cluster_center]


def write_binary(filename, ids, vectors):
    n = len(ids)
    dim = len(vectors[0])
    with open(filename, "wb") as f:
        f.write(struct.pack("<II", n, dim))
        for i in range(n):
            id_bytes = ids[i].encode("utf-8")
            f.write(struct.pack("<I", len(id_bytes)))
            f.write(id_bytes)
            f.write(struct.pack(f"<{dim}f", *vectors[i]))


def write_csv(filename, ids, vectors):
    with open(filename, "w") as f:
        w = csv.writer(f)
        w.writerow(["id"] + [f"v{i}" for i in range(len(vectors[0]))])
        for i in range(n := len(ids)):
            w.writerow([ids[i]] + vectors[i])


def generate_random(dim, n, data_dir, rng):
    kind = "random"
    ids = [f"vec-{i}" for i in range(n)]
    vectors = [random_vec(dim, rng) for _ in range(n)]
    basename = f"dim{dim}_{n}_{kind}"
    write_binary(os.path.join(data_dir, basename + ".bin"), ids, vectors)
    write_csv(os.path.join(data_dir, basename + ".csv"), ids, vectors)
    return basename


def generate_clustered(dim, n, data_dir, rng, num_clusters=5):
    kind = "clustered"
    centers = [random_vec(dim, rng) for _ in range(num_clusters)]
    ids = [f"vec-{i}" for i in range(n)]
    vectors = [clustered_vec(dim, rng, centers[rng.randint(0, num_clusters - 1)]) for _ in range(n)]
    basename = f"dim{dim}_{n}_{kind}"
    write_binary(os.path.join(data_dir, basename + ".bin"), ids, vectors)
    write_csv(os.path.join(data_dir, basename + ".csv"), ids, vectors)
    return basename


def try_real_embeddings(dim, n, data_dir):
    """Try to generate embeddings using sentence-transformers if available."""
    try:
        from sentence_transformers import SentenceTransformer  # type: ignore
        model = SentenceTransformer("all-MiniLM-L6-v2")
    except Exception:
        return None

    model_dim = model.get_sentence_embedding_dimension()
    if model_dim != dim:
        return None

    sentences = [f"This is a sample sentence number {i} for generating embeddings."
                 for i in range(n)]
    embeddings = model.encode(sentences)
    ids = [f"sent-{i}" for i in range(n)]
    vectors = [emb.tolist() for emb in embeddings]

    basename = f"dim{dim}_{n}_real"
    write_binary(os.path.join(data_dir, basename + ".bin"), ids, vectors)
    write_csv(os.path.join(data_dir, basename + ".csv"), ids, vectors)
    return basename


def main():
    parser = argparse.ArgumentParser(description="Generate benchmark datasets")
    parser.add_argument("--dims", nargs="+", type=int, default=None,
                        help="Dimensions to generate (default: 128 384 768 1536)")
    parser.add_argument("--sizes", nargs="+", type=int, default=None,
                        help="Sizes per dim (default: 1000 10000)")
    parser.add_argument("--real", action="store_true",
                        help="Attempt real embeddings via sentence-transformers")
    parser.add_argument("--clustered", action="store_true",
                        help="Generate clustered (non-uniform) vectors")
    args = parser.parse_args()

    dims = args.dims or [128, 384, 768, 1536]
    sizes = args.sizes or [1000, 10000]
    rng = random.Random(SEED)


    os.makedirs(DATA_DIR, exist_ok=True)

    generated = []

    for dim in dims:
        for n in sizes:
            print(f"  random  dim={dim:4d}  n={n:>6d}  ...", end=" ", flush=True)
            basename = generate_random(dim, n, DATA_DIR, rng)
            generated.append(basename)
            sz = os.path.getsize(os.path.join(DATA_DIR, basename + ".bin"))
            print(f"{sz / 1024:.0f} KB")

            if args.clustered:
                print(f"  clust  dim={dim:4d}  n={n:>6d}  ...", end=" ", flush=True)
                basename = generate_clustered(dim, n, DATA_DIR, rng)
                generated.append(basename)
                sz = os.path.getsize(os.path.join(DATA_DIR, basename + ".bin"))
                print(f"{sz / 1024:.0f} KB")

            if dim == 384 and args.real:
                print(f"  real   dim={dim:4d}  n={n:>6d}  ...", end=" ", flush=True)
                basename = try_real_embeddings(dim, n, DATA_DIR)
                if basename:
                    generated.append(basename)
                    sz = os.path.getsize(os.path.join(DATA_DIR, basename + ".bin"))
                    print(f"{sz / 1024:.0f} KB")
                else:
                    print("SKIP (sentence-transformers not available or dim mismatch)")

    # Generate larger datasets for search benchmarks (opt-in, skipped by default)
    # Use --large to enable: python3 generate_data.py --large
    if getattr(args, "large", False):
        for dim in [384]:
            for n in [100000, 1000000]:
                print(f"  random  dim={dim:4d}  n={n:>7d}  ...", end=" ", flush=True)
                basename = generate_random(dim, n, DATA_DIR, rng)
                generated.append(basename)
                sz = os.path.getsize(os.path.join(DATA_DIR, basename + ".bin"))
                print(f"{sz / 1024:.0f} KB")

    total = sum(os.path.getsize(os.path.join(DATA_DIR, f"{b}.bin")) for b in generated)
    print(f"\nGenerated {len(generated)} datasets, {total / (1024*1024):.1f} MB total in {DATA_DIR}/")


if __name__ == "__main__":
    main()
