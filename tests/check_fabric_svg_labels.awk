function attribute(line, name,    pieces, tail) {
    split(line, pieces, name "=\"")
    if (length(pieces) < 2)
        return ""
    split(pieces[2], tail, "\"")
    return tail[1]
}

/<text class="port" data-endpoint=/ {
    count++
    x[count] = attribute($0, "x") + 0
    y[count] = attribute($0, "y") + 0
    anchor = attribute($0, "text-anchor")
    text = $0
    sub(/^.*>/, "", text)
    sub(/<\/text>.*$/, "", text)
    width = length(text) * 6.1
    left[count] = anchor == "middle" ? x[count] - width / 2 : \
        anchor == "end" ? x[count] - width : x[count]
    right[count] = left[count] + width
    top[count] = y[count] - 11
    bottom[count] = y[count] + 3
    label[count] = attribute($0, "data-endpoint")
}

END {
    for (first = 1; first <= count; first++)
        for (second = first + 1; second <= count; second++)
            if (left[first] < right[second] && left[second] < right[first] && \
                top[first] < bottom[second] && top[second] < bottom[first]) {
                print "fabric SVG port labels overlap: " label[first] " and " label[second] > "/dev/stderr"
                exit 1
            }
}
