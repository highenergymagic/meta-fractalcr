# Upstream vex clears its append-only summary index at BuildCompleted. A killed
# build never reaches that event; reusing the index duplicates package entries
# in the next report. Start with the same empty index as a completed build.
# Individual recipe reports and vulnerability data remain intact.
python fractal_vex_begin () {
    path = e.data.getVar('CVE_CHECK_SUMMARY_INDEX_PATH')
    if path:
        bb.utils.remove(path)
}
addhandler fractal_vex_begin
fractal_vex_begin[eventmask] = "bb.event.BuildStarted"
