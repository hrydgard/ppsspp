# Used to convert git describe strings like 1.17.1-421-g0100c6b32b
# to three dot-separated integers, as iOS wants for the short version.

$version = $ARGV[0];

# Strip a "v" if the version starts with it.
$version =~ s/^v//;

# Use multiple regexes to parse the various formats we may encounter.
# I don't know perl better than this.

if ($version =~ /^(\d+)\.(\d+)\.(\d+)-(\d+)/) {
    my $major = $1 * 10000 + $2;
    my $minor = $3;
    my $rev = $4;
    print $major . "." . $minor . "." . $rev . "\n"; # Output: 1017.1.421
    exit
}

if ($version =~ /^(\d+)\.(\d+)\.(\d+)/) {
    my $major = $1 * 10000 + $2;
    my $minor = $3;
    my $rev = "0";
    print $major . "." . $minor . "." . $rev . "\n"; # Output: 1017.0.0
    exit
}

if ($version =~ /^(\d+)\.(\d+)/) {
    my $major = $1 * 10000 + $2;
    my $minor = "0";
    my $rev = "0";
    print $major . "." . $minor . "." . $rev . "\n"; # Output: 1017.0.0
    exit
}

die($version)