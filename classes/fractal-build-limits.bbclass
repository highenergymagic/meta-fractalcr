# Keep compiler memory bounded without exporting Go settings into unrelated
# recipes (which would invalidate their upstream shared-state cache entries).
python () {
    if bb.data.inherits_class('goarch', d):
        for name, value in (('GOMAXPROCS', '2'), ('GOMEMLIMIT', '600MiB')):
            d.setVar(name, value)
            d.setVarFlag(name, 'export', '1')
}

# OE otherwise uses BB_NUMBER_THREADS for both network probes and compilation.
# Independent HEAD requests can run concurrently without scheduling additional
# compiler processes. Change only the temporary datastore used by the checker.
BB_HASHCHECK_FUNCTION:fractal = "fractal_sstate_checkhashes"
def fractal_sstate_checkhashes(sq_data, d, **kwargs):
    lookup = bb.data.createCopy(d)
    lookup.setVar("BB_NUMBER_THREADS", "16")
    return sstate_checkhashes(sq_data, lookup, **kwargs)
