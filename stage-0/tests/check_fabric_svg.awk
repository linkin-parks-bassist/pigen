function attribute(line, name,    pieces, tail) {
    split(line, pieces, name "=\"")
    if (length(pieces) < 2)
        return ""
    split(pieces[2], tail, "\"")
    return tail[1]
}

/<circle class="(unit|router)"/ {
    count++
    x[count] = attribute($0, "cx") + 0
    y[count] = attribute($0, "cy") + 0
    if (attribute($0, "data-unit") != "")
        radius[count] = attribute($0, "r") + 14
    else
        radius[count] = 52
    label[count] = attribute($0, "data-unit")
    if (label[count] == "")
        label[count] = attribute($0, "data-router")
}

END {
    if (!count) {
        print "fabric SVG contains no unit or router nodes" > "/dev/stderr"
        exit 1
    }
    for (left = 1; left <= count; left++)
        for (right = left + 1; right <= count; right++) {
            dx = x[left] - x[right]
            dy = y[left] - y[right]
            minimum = radius[left] + radius[right]
            if (dx * dx + dy * dy < minimum * minimum) {
                print "fabric SVG nodes overlap: " label[left] " and " label[right] > "/dev/stderr"
                exit 1
            }
        }
}
