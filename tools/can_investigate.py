#!/usr/bin/env python3
# Copyright (c) 2026 Aritra Saha. All rights reserved.
"""Offline CAN investigation. Suggestions are UNVERIFIED; never edits firmware."""
from __future__ import annotations
import argparse
import bisect
import csv
from dataclasses import dataclass
from collections import defaultdict
from pathlib import Path
import json
import math
import re
import statistics

RX = re.compile(r"RX t=(\d+) id=([0-9A-Fa-f]{1,8}) ext=([01]) rtr=([01]) dlc=(\d+) data=([0-9A-Fa-f]*) ([A-Z_]+)")
SCALES = (1, .25, .5, 2, 4, .125, .0625, .03125, .015625)

@dataclass(frozen=True)
class Frame:
    timestamp_ms: int
    raw_timestamp_ms: int
    identifier: int
    extended: bool
    remote: bool
    dlc: int
    data: bytes
    line: int

def read_log(path: Path, limit: int = 500000):
    frames, issues, counters = [], [], {}
    epoch = 0
    previous = None
    synthetic = False
    records = 0
    with path.open(encoding="utf-8", errors="strict") as source:
        for number, line in enumerate(iter(lambda: source.readline(4097), ""), 1):
            if len(line) > 4096:
                raise ValueError(f"Log line {number} exceeds 4096 characters.")
            line = line.strip()
            if line.startswith("REVIX LINK mode=") and previous is not None:
                raise ValueError(f"Startup banner at line {number}; split logs at a reboot/session boundary.")
            if "SYNTHETIC" in line or "DEMO_SIMULATED" in line:
                synthetic = True
            if not line.startswith("RX "):
                if line.startswith(("CAN ", "APP ", "LOG ")):
                    for key, value in re.findall(r"(\w+)=(\d+)", line):
                        if key in ("missed","overrun","busErrors","TEC","REC","RX","acceptedFrames","logDropped","dropped"):
                            counters[key] = max(counters.get(key, 0), int(value))
                continue
            records += 1
            if records > limit:
                raise ValueError(f"More than {limit} RX records including malformed records; split the capture or increase --max-frames.")
            match = RX.fullmatch(line)
            if not match:
                issues.append({"line": number, "reason": "invalid RX syntax"})
                continue
            t, ident, ext, remote, dlc, data, _result = match.groups()
            t, ident, dlc = int(t), int(ident, 16), int(dlc)
            remote, ext = remote == "1", ext == "1"
            if t > 0xFFFFFFFF or dlc > 8 or ident > (0x1FFFFFFF if ext else 0x7FF) or len(data) != (0 if remote else dlc * 2):
                issues.append({"line": number, "reason": "invalid ID/DLC/payload/timestamp"})
                continue
            if previous is not None and t < previous:
                if previous - t > 0x80000000:
                    epoch += 1 << 32
                else:
                    raise ValueError(f"Timestamp moved backward at line {number}; split logs at a reboot/session boundary.")
            previous = t
            frames.append(Frame(t + epoch, t, ident, ext, remote, dlc, bytes.fromhex(data), number))
    return frames, issues, counters, synthetic

def read_reference(path: Path, offset: float):
    rows = []
    with path.open(newline="", encoding="utf-8") as source:
        reader = csv.DictReader(source)
        if not {"timestamp_ms", "rpm"} <= set(reader.fieldnames or []):
            raise ValueError("Reference CSV requires timestamp_ms,rpm columns.")
        for row in reader:
            t, rpm = float(row["timestamp_ms"]) + offset, float(row["rpm"])
            if not math.isfinite(t) or not math.isfinite(rpm) or t < 0 or not 0 <= rpm <= 65535:
                raise ValueError("Invalid reference timestamp/RPM.")
            if rows and t <= rows[-1][0]:
                raise ValueError("Reference times must be strictly increasing.")
            rows.append((t, rpm))
    if not rows:
        raise ValueError("Empty reference CSV.")
    return rows

def align(frames, references, max_skew):
    times = [row[0] for row in references]
    aligned = []
    # One closest CAN frame per reference sample and frame group. Dense CAN
    # traffic must not multiply the apparent number of independent measurements.
    chosen = {}
    for frame in frames:
        at = bisect.bisect_left(times, frame.timestamp_ms)
        choices = [i for i in (at-1, at) if 0 <= i < len(times)]
        if not choices:
            continue
        index = min(choices, key=lambda i: (abs(times[i]-frame.timestamp_ms), i))
        skew = frame.timestamp_ms - times[index]
        if abs(skew) > max_skew:
            continue
        if index not in chosen or abs(skew) < abs(chosen[index][2]):
            chosen[index] = (frame, references[index][1], skew, index)
    aligned.extend(chosen[i] for i in sorted(chosen))
    return aligned

def rmse(values):
    return math.sqrt(statistics.fmean(x*x for x in values))

def rank_fields(group, references, max_skew):
    paired = align(group, references, max_skew)
    if len(paired) < 6:
        return []
    ys = [row[1] for row in paired]
    span = max(ys) - min(ys)
    if span <= 0:
        return []
    split = len(paired)*2//3 if len(paired) >= 12 else len(paired)
    train, validation = paired[:split], paired[split:]
    candidates = []
    dlc = group[0].dlc
    for width in (1,2,3,4):
        for offset in range(dlc-width+1):
            for order in (("little",) if width == 1 else ("little","big")):
                values = [int.from_bytes(row[0].data[offset:offset+width], order) for row in paired]
                if len(set(values[:split])) < 3:
                    continue
                xs = values[:split]
                ytrain = ys[:split]
                xm, ym = statistics.fmean(xs), statistics.fmean(ytrain)
                variance = sum((x-xm)**2 for x in xs)
                slope = sum((x-xm)*(y-ym) for x,y in zip(xs,ytrain))/variance
                fits = [(scale, 0., "fixed_common_scale") for scale in SCALES]
                if slope > 0:
                    fits.append((slope, ym-slope*xm, "fitted_affine"))
                scored = []
                for scale, shift, kind in fits:
                    residuals = [x*scale+shift-y for x,y in zip(values,ys)]
                    train_error = rmse(residuals[:split])
                    validation_error = rmse(residuals[split:]) if validation else None
                    score = (validation_error if validation_error is not None else train_error)
                    scored.append((score, kind=="fitted_affine", scale, shift, kind, train_error, validation_error))
                best = min(scored, key=lambda row:(round(row[0],8),row[1]))
                error, _, scale, shift, kind, train_error, validation_error = best
                denominator = math.sqrt(sum((x-xm)**2 for x in xs)*sum((y-ym)**2 for y in ytrain))
                correlation = sum((x-xm)*(y-ym) for x,y in zip(xs,ytrain))/denominator if denominator else 0
                counters = [((b-a) % (1 << (8*width))) == 1 for a,b in zip(values,values[1:])]
                candidate = {
                    "status":"UNVERIFIED", "id":f"{group[0].identifier:08X}",
                    "extended":group[0].extended, "dlc":dlc, "byte_offset":offset,
                    "width_bits":width*8, "byte_order":order,
                    "start_bit":offset*8,
                    "bit_convention":"LSB0" if order=="little" else "MSB0 network stream (not DBC)",
                    "scale":scale, "offset_rpm":shift, "fit":kind,
                    "aligned_reference_samples":len(paired), "training_samples":len(train),
                    "validation_samples":len(validation), "rpm_span":span,
                    "correlation_training":correlation, "train_rmse_rpm":train_error,
                    "validation_rmse_rpm":validation_error, "normalized_error":error/span,
                    "quality_flags": (["low_rpm_span"] if span<500 else []) + (["no_holdout"] if not validation else []),
                    "counter_like": bool(counters) and statistics.fmean(counters) > .9,
                    "max_alignment_skew_ms":max(abs(row[2]) for row in paired),
                    "caution":"No independent-session, validity, counter, checksum or electrical qualification.",
                    "_pairs":paired,
                }
                candidates.append(candidate)
    return sorted(candidates,key=lambda c:(round(c["normalized_error"],10),c["fit"]=="fitted_affine",c["width_bits"],c["id"]))

def investigate(log: Path, reference: Path | None, offset=0., skew=100., top=10, limit=500000, vector_limit=1000):
    frames, issues, counters, synthetic = read_log(log,limit)
    grouped = defaultdict(list)
    for f in frames:
        grouped[(f.identifier,f.extended,f.dlc,f.remote)].append(f)
    refs = read_reference(reference,offset) if reference else []
    summary, ranked = [], []
    for key, group in sorted(grouped.items()):
        ident, ext, dlc, remote = key
        intervals = [b.timestamp_ms-a.timestamp_ms for a,b in zip(group,group[1:])]
        duration = group[-1].timestamp_ms-group[0].timestamp_ms
        changes=[]
        if not remote:
            for byte in range(dlc):
                values = [f.data[byte] for f in group]
                mask=0
                for value in values: mask |= value ^ values[0]
                changes.append({"byte":byte,"distinct":len(set(values)),"min":min(values),"max":max(values),"changing_mask":f"{mask:02X}"})
        summary.append({"id":f"{ident:08X}","extended":ext,"dlc":dlc,"remote":remote,
            "frames":len(group),"observed_hz":(len(group)-1)*1000/duration if duration else None,
            "median_gap_ms":statistics.median(intervals) if intervals else None,
            "maximum_gap_ms":max(intervals) if intervals else None,"bytes":changes})
        if refs and not remote:
            ranked.extend(rank_fields(group,refs,skew))
    ranked.sort(key=lambda c:(round(c["normalized_error"],10),c["fit"]=="fitted_affine",c["width_bits"],c["id"]))
    vectors=[]
    for rank, candidate in enumerate(ranked[:top],1):
        candidate["rank"]=rank
        paired = candidate.pop("_pairs")
        indexes = sorted({round(i*(len(paired)-1)/(vector_limit-1)) for i in range(vector_limit)}) if len(paired)>vector_limit and vector_limit>1 else list(range(min(len(paired),vector_limit)))
        candidate["exported_vector_count"] = len(indexes)
        for index in indexes:
            row = paired[index]
            raw = int.from_bytes(row[0].data[candidate["byte_offset"]:candidate["byte_offset"]+candidate["width_bits"]//8],candidate["byte_order"])
            f, rpm, alignment, _ = row
            vectors.append({"candidate_rank":rank,"status":"UNVERIFIED","source_line":f.line,
                "timestamp_ms":f.timestamp_ms,"raw_timestamp_ms":f.raw_timestamp_ms,
                "id":f"{f.identifier:08X}","extended":f.extended,"dlc":f.dlc,"payload_hex":f.data.hex().upper(),
                "raw_field":raw,"reference_rpm":rpm,"alignment_skew_ms":alignment,
                "predicted_rpm":raw*candidate["scale"]+candidate["offset_rpm"]})
    # Drop internal records from every candidate, including those outside top N.
    public=[{k:v for k,v in c.items() if not k.startswith("_")} for c in ranked[:top]]
    return {"schema_version":1,"status":"UNVERIFIED","synthetic_input":synthetic,
        "frames":len(frames),"invalid_log_records":issues,"observed_counters_max":counters,
        "alignment":{"reference_offset_ms":offset,"maximum_skew_ms":skew,"method":"nearest frame, one per reference sample, no interpolation"},
        "messages":summary,"candidates":public},vectors

def write_report(out: Path, result, vectors):
    out.mkdir(parents=True,exist_ok=True)
    (out/"candidates.json").write_text(json.dumps(result,indent=2,allow_nan=False)+"\n")
    (out/"regression_vectors.json").write_text(json.dumps({"status":"UNVERIFIED","synthetic_input":result["synthetic_input"],"vectors":vectors},indent=2,allow_nan=False)+"\n")
    lines=["# Passive CAN investigation","", "**UNVERIFIED suggestions. This tool never enables firmware profiles.**","",
        f"Frames: {result['frames']}. Invalid log records: {len(result['invalid_log_records'])}. Synthetic input detected: {result['synthetic_input']}.","",
        "## Observed messages","", "| ID | Extended | DLC | RTR | Frames | Observed Hz | Maximum gap ms |","| --- | --- | --- | --- | --- | --- | --- |"]
    for m in result["messages"]:
        hz=f"{m['observed_hz']:.2f}" if m["observed_hz"] is not None else "unknown"
        lines.append(f"| {m['id']} | {m['extended']} | {m['dlc']} | {m['remote']} | {m['frames']} | {hz} | {m['maximum_gap_ms']} |")
    lines+=["","## Changing bytes",""]
    for m in result["messages"]:
        changing=", ".join(f"{b['byte']}:0x{b['changing_mask']}" for b in m["bytes"] if b["distinct"]>1) or "none"
        lines.append(f"- {m['id']} ext={m['extended']} dlc={m['dlc']} changing byte:mask = {changing}")
    lines+=["","## Ranked numerical candidates","",
        "| Rank | ID / ext | Field | Scale | Offset RPM | Train RMSE | Validation RMSE | Unique reference samples |",
        "| --- | --- | --- | --- | --- | --- | --- | --- |"]
    for c in result["candidates"]:
        val=f"{c['validation_rmse_rpm']:.3f}" if c["validation_rmse_rpm"] is not None else "NOT AVAILABLE"
        lines.append(f"| {c['rank']} | {c['id']} / {c['extended']} | byte {c['byte_offset']}, {c['width_bits']}bit {c['byte_order']} | {c['scale']:.9g} | {c['offset_rpm']:.9g} | {c['train_rmse_rpm']:.3f} | {val} | {c['aligned_reference_samples']} |")
    lines+=["","## Limits and required review","",
        "- Alignment uses local dequeue time, not the hardware CAN arrival time. Calibrate clock offset/latency using an independent event; do not choose offset just to maximize correlation.",
        "- Out-of-window samples are omitted. Dense frames sharing a reference timestamp count once. Check source gaps, the exported skews and the reference sample rate.",
        "- The last third of samples is a chronological holdout when at least 12 align. This is not an independent capture session. Fewer samples have no holdout; constant/low-span traces cannot establish a mapping.",
        "- Only unsigned byte-aligned 8/16/24/32-bit fields are searched, with common fixed scales or a learned positive affine fit. Bitfields, signed fields, proprietary compression and DBC Motorola numbering need explicit investigation.",
        "- Counter, checksum and multiplexed bytes can correlate accidentally. Counter-like flags are hints only. No checksum algorithm or validity bit is proven by a good numeric fit.",
        "- Regression exports are representative review samples (default at most 1000 per candidate), not a full capture replacement.",
        "- Observed frequencies are estimates, not bus timing guarantees. Review invalid records and driver/log losses; serial logging is bounded and can drop data.",
        "- Repeat with independently measured RPM, engine-off/zero, idle, rising and falling points, repeated sessions and a held-out recording. Reject/revise candidates freely; the LED engine does not change.",
        "- Connector pins, actual breakout RS wiring/termination, reset passivity and protected automotive power require physical qualification.",
        "",f"Observed maximum counters: {json.dumps(result['observed_counters_max'],sort_keys=True)}",
        "", "Outputs are review records only. No firmware configuration is written."]
    (out/"report.md").write_text("\n".join(lines)+"\n")

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log",type=Path)
    parser.add_argument("--reference",type=Path)
    parser.add_argument("--reference-offset-ms",type=float,default=0)
    parser.add_argument("--max-skew-ms",type=float,default=100)
    parser.add_argument("--top",type=int,default=10)
    parser.add_argument("--max-frames",type=int,default=500000)
    parser.add_argument("--max-vectors-per-candidate",type=int,default=1000)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    if not math.isfinite(args.reference_offset_ms) or not math.isfinite(args.max_skew_ms) or args.max_skew_ms<0 or not 1<=args.top<=100 or args.max_frames<1 or not 1<=args.max_vectors_per_candidate<=10000:
        parser.error("Invalid alignment, rank count or frame limit.")
    try:
        result,vectors=investigate(args.log,args.reference,args.reference_offset_ms,args.max_skew_ms,args.top,args.max_frames,args.max_vectors_per_candidate)
        write_report(args.output,result,vectors)
    except (ValueError,OSError,csv.Error) as error:
        parser.exit(2,f"CAN investigation failed: {error}\n")
    print(f"UNVERIFIED: {result['frames']} frames, {len(result['messages'])} message groups, {len(result['candidates'])} candidates.")
    if result["candidates"]:
        c=result["candidates"][0]
        print(f"Top: id={c['id']} ext={int(c['extended'])} byte={c['byte_offset']} width={c['width_bits']} {c['byte_order']} scale={c['scale']:.9g} offset={c['offset_rpm']:.9g} RPM")
    print(f"Report: {args.output/'report.md'}; firmware profiles were not changed.")
if __name__=="__main__":
    main()
