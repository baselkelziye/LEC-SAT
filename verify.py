import itertools

def load_cnf(path):
    clauses = []
    nvars = 0
    with open(path) as f:
        for line in f:
            if line.startswith('c') or not line.strip():
                continue
            if line.startswith('p'):
                nvars = int(line.split()[2])
                continue
            lits = [int(x) for x in line.split()][:-1]
            clauses.append(lits)
    return nvars, clauses

def load_iomap(path):
    entries = []
    with open(path) as f:
        for line in f:
            if line.startswith('c') or not line.strip():
                continue
            d, port, bit, var, pol = line.split()
            entries.append((d, port, int(bit), int(var), pol))
    return entries

def bcp_evaluate(nvars, clauses, forced):
    # forced: dict var -> bool. Propagate until fixpoint; combinational
    # Tseitin CNF should fully determine every variable this way.
    val = dict(forced)
    changed = True
    while changed:
        changed = False
        for cl in clauses:
            unassigned = [l for l in cl if abs(l) not in val]
            sat = any((l > 0) == val.get(abs(l), None) for l in cl if abs(l) in val)
            if sat:
                continue
            if len(unassigned) == 1:
                l = unassigned[0]
                val[abs(l)] = (l > 0)
                changed = True
            elif len(unassigned) == 0:
                raise RuntimeError("clause violated / UNSAT under forced assignment: " + str(cl))
    missing = [v for v in range(1, nvars+1) if v not in val]
    if missing:
        raise RuntimeError(f"{len(missing)} variables not determined by BCP, e.g. {missing[:5]}")
    return val

def alu4_ref(a, b, op):
    a &= 0xF; b &= 0xF
    if op == 0:
        s = a + b
        return s & 0xF, (s >> 4) & 1
    elif op == 1:
        s = (a - b) & 0x1F
        return s & 0xF, (s >> 4) & 1
    elif op == 2:
        return a & b, 0
    else:
        return a | b, 0

def eval_circuit(cnf_path, iomap_path, a, b, op):
    nvars, clauses = load_cnf(cnf_path)
    io = load_iomap(iomap_path)
    forced = {}
    def set_bits(port, width, value):
        for bit in range(width):
            for d, p, b_, var, pol in io:
                if d == 'i' and p == port and b_ == bit:
                    bitval = (value >> bit) & 1
                    truth = bool(bitval) if pol == '+' else not bool(bitval)
                    forced[var] = truth
    set_bits('a', 4, a)
    set_bits('b', 4, b)
    set_bits('op', 2, op)
    val = bcp_evaluate(nvars, clauses, forced)
    def get_bits(port, width):
        out = 0
        for bit in range(width):
            for d, p, b_, var, pol in io:
                if d == 'o' and p == port and b_ == bit:
                    v = val[var]
                    if pol == '-':
                        v = not v
                    out |= (1 if v else 0) << bit
        return out
    result = get_bits('result', 4)
    cout = get_bits('cout', 1)
    return result, cout

mismatches = 0
for a, b, op in itertools.product(range(16), range(16), range(4)):
    exp_r, exp_c = alu4_ref(a, b, op)
    r1, c1 = eval_circuit('plain_synth/alu4.cnf', 'plain_synth/alu4.iomap', a, b, op)
    r2, c2 = eval_circuit('optimized_synth/alu4.cnf', 'optimized_synth/alu4.iomap', a, b, op)
    if (r1, c1) != (exp_r, exp_c) or (r2, c2) != (exp_r, exp_c):
        mismatches += 1
        if mismatches <= 5:
            print(f"MISMATCH a={a} b={b} op={op}: ref=({exp_r},{exp_c}) netlist=({r1},{c1}) aig=({r2},{c2})")

print(f"done: {mismatches} mismatches out of 1024 combinations")
raise SystemExit(1 if mismatches else 0)
