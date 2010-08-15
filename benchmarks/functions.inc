function check_for_errors {
    if grep 'WARNING:|ERROR:|PANIC:' "$@" >/dev/null
    then
	    echo "$0 failed" 1>&2
	    echo "See bench_sms*.log for info" 1>&2
	    exit 1
    fi
}

function plot_commands {
    echo "set terminal $1"
    echo "set xlabel \"$2\""
    echo "set ylabel \"$3\""
    if [ "$5" = "" ]
    then
	echo -n "plot \"$4\" notitle with lines"
    else
	echo -n "plot \"$4\" title \"$5\" with lines"
    fi
    shift 5
    while [ "$2" != "" ]
    do
        if [ "$2" == "" ]
	then
	    echo -n ", \"$1\" notitle with lines"
	else
	    echo -n ", \"$1\" title \"$2\" with lines"
	fi
	shift 2
    done
    echo ""
}

function plot {
    base="$1"
    shift
    plot_commands png "$@" | gnuplot > "$base.png"
    plot_commands "postscript eps" "$@" | gnuplot > "$base.ps"
}
