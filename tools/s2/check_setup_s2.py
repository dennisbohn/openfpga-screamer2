"""The verdict on the set-ups' cases of tools/s2/check_car.sh (menus row 12, docs/screamer2/driving.md "The set-up"): reads the log it wrote (build/s2/car_check.txt).

Each case u_<scenario>_<tag> is the player's car with a set-up of CUSTOMIZE (setups.txt) run through firmware/s2car.c in float32 with the place cut to whole numbers as the
game does, against the trace of the original's own routines (OpenScreamer, tools/s2/ref_trace/ref_run.sh setups, the player's set-up in the race record, 0x14e20 making the
car of it). It must follow the reference: the whole run's largest difference of the speed and the heading small and the car on the reference's speed for most of the run (the first step
it leaves it by more than 0.001: a place cut one unit the other way by a float32 sum, which then shows as a speed of 0.3 for good, happens in the reference's own checks too:
a_flat, b_origin, g_manual). The same run with the set-up left out (the negative control: the standard car) must NOT follow it: unless the set-up is the standard car's own (u_*_std,
and the wet tyres on track 1, whose own tyres they are), it leaves the reference's speed within the first 150 steps (the scenarios steer from step 100: before it a straight run may be the same) and by more than 10 and 4 times as much as the case itself.

Usage: check_setup_s2.py <car_check.txt>; exit code 1 if a case or its control fails.
"""
import sys

SAME_AS_STANDARD = ("std", "t1")        # the tag of a set-up that is the standard car on track 1 (the standard's tyres there are the rain tyres, B - Wet)


def sections(path):
    out = {}
    for sec in open(path).read().split("\n== "):
        sec = sec.lstrip("= ")
        lines = sec.split("\n")
        head = lines[0]
        if not head.startswith("u_"):
            continue
        name = head.split(" ")[0]
        q, first = {}, None
        for line in lines[2:]:
            f = line.split()
            if len(f) == 4:
                q[f[0]] = float(f[1])
            elif line.startswith("first speed difference"):
                first = None if "none" in line else int(line.split("step ")[1].split(" ")[0])
        out.setdefault(name, {})["neg" if "negative control" in head else "ok"] = (q, first)
    return out


def main():
    cases = sections(sys.argv[1])
    if not cases:
        print("setups: no case in the log (tools/s2/ref_trace/ref_run.sh setups makes the traces)")
        return 1
    bad = 0
    for name, r in sorted(cases.items()):
        tag = name.split(".")[0].split("_")[-1]
        if "ok" not in r:
            print("FAIL %s: not run" % name)
            bad += 1
            continue
        q, first = r["ok"]
        ok = q["speed"] < 5.0 and q["heading"] < 0.1 and (first is None or first >= 150)
        print("%s %-34s speed %.3g heading %.3g rpm %.3g steer %.3g, on the reference's speed %s" % ("PASS" if ok else "FAIL", name.split(".")[0], q["speed"], q["heading"], q["rpm"], q["steer"],
                                                                                              "to the end" if first is None else "up to step %d" % first))
        bad += not ok
        if tag in SAME_AS_STANDARD:
            continue
        if "neg" not in r:
            print("FAIL %s: no negative control" % name)
            bad += 1
            continue
        nq, nfirst = r["neg"]
        diff = nq["speed"] > 10.0 and nq["speed"] > 4.0 * q["speed"] and nfirst is not None and nfirst < 150
        print("%s %-34s negative control: the standard car leaves the reference's speed at step %s by up to %.3g" % ("PASS" if diff else "FAIL", name.split(".")[0], nfirst, nq["speed"]))
        bad += not diff
    print("setups: %d cases, %d failures" % (len(cases), bad))
    return 1 if bad else 0


sys.exit(main())
