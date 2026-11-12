"""Round 3 summary: structural completeness vs differential validation."""
import json, sys, collections
tot = collections.Counter(); rows = []
for path in sys.argv[1:]:
    stem = path.split('/')[-1][:-6]
    fin, summ, runs = {}, {}, collections.defaultdict(list)
    rank = {"final": 3, "generic_check": 2, "generic_rejected": 1}
    for l in open(path):
        if not l.strip(): continue
        try: r = json.loads(l)
        except json.JSONDecodeError:
            tot["bad_json_lines"] += 1; continue
        ph = r.get("phase")
        if ph == "validate":
            if r.get("concrete") == "crash":
                tot["oracle_crash"] += 1; continue  # compiler failed internally: no evidence
            runs[r["function"]].append(r); continue
        if ph == "validation_summary": summ[r["function"]] = r; continue
        if ph == "validate_skipped": continue
        p = fin.get(r["function"])
        if not p or rank.get(ph, 0) >= rank.get(p["phase"], 0): fin[r["function"]] = r
    for n, r in fin.items():
        sh = r["shadow"]; s = summ.get(n); v = runs.get(n, [])
        complete = sh["status"] == "complete"
        tot["entries"] += 1; tot["complete" if complete else "incomplete:" + sh["status"]] += 1
        if complete:
            tot["complete_runs"] += len(v); tot["complete_agree"] += sum(x["agree"] for x in v)
            tot["complete_unknown_pred"] += sum(x["predicted"] == "unknown" for x in v)
            tot["complete_accept"] += sum(x["concrete"] == "accept" for x in v)
            tot["complete_reject"] += sum(x["concrete"] == "reject" for x in v)
            tot["complete_result_unobserved"] += sum(not x.get("result_observed", True) for x in v)
            tot["validated" if s and s["validated"] else "complete_unvalidated"] += 1
            if s and s["trait_bounded"]: tot["trait_bounded_validated" if s["validated"] else "trait_bounded_unvalidated"] += 1
        tot["oracle_splits"] += s["oracle_splits"] if s else 0
        rows.append((stem, n, sh["status"], bool(s and s["validated"]), len(v), sum(x["agree"] for x in v),
                     (sh.get("missing_facts", []) + sh.get("missing_representation", []))[:3]))
        if complete:
            for x in v:
                if not x["agree"]:
                    access = ""
                    if x.get("access_agrees") is False:
                        access = f' access predicted {x.get("predicted_access")} checker {x.get("concrete_access")}'
                    print("DISAGREE", stem, n, x["sigma"], x["predicted"], x["predicted_reason"][:70], "|", x["concrete"], x["concrete_reason"][:90] + access)
if "-v" in sys.argv[0:0] or True:
    for row in rows:
        if row[2] != "complete" or not row[3]: print("LEDGER", *row)
print(json.dumps(tot, indent=0))
