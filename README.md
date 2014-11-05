# PbfParser

As its name suggests is a gem to parse [Open Street Map](http://www.openstreetmap.org/) [PBF](http://wiki.openstreetmap.org/wiki/Pbf) files with ease.

**NOTE: Since v0.2.0 protobuf 2.6+ is required.**

## Installation

You need protobuf-c and zlib. On OS X you can use Homebrew to install them easily.

```shell
$ brew install zlib
$ brew install protobuf-c
```

On Debian-like distros, install zlib1g-dev and libprotobuf-c0-dev.

Add this line to your application's Gemfile:

```ruby
gem 'pbf_parser'
```

And then execute:

```shell
$ bundle
```

Or install it yourself as:

```shell
$ gem install pbf_parser
```

## Usage

### Sequential access

```ruby
> pbf = PbfParser.new("planet.osm.pbf")
=> #<PbfParser:0x008fa8d1080080>
```

This reads the OSMHeader fileblock and then parses the first OSMData fileblock.

You can parse a FileBlock each time by calling #next. It returns true until the EOF is reached, then returns false. Take in mind that when EOF is reached the data of the last FileBlock found is kept.

```ruby
> pbf.next
=> true

> pbf.next
=> true

[...]

# EOF reached
> pbf.next
=> false
```

You can access the parsed data calling #data, it returns a hash containing the nodes, ways and relations found on the fileblock that has been read

```ruby
> pbf.data[:nodes].first unless pbf.data[:nodes].empty?
=> {:id=>21911863, :lat=>43.7370125, :lon=>7.422028, :version=>5, :timestamp=>1335970231000, :changeset=>11480240, :uid=>378737, :user=>"Scrup", :tags=>{}}
```

You can also access them directly through the #nodes, #ways and #relations shorcut methods

```ruby
> pbf.ways.first
=> {:id=>4097656, :version=>6, :timestamp=>1357851917000, :changeset=>14602065, :uid=>852996, :user=>"Mg2", :tags=>{"highway"=>"primary", "name"=>"Avenue Princesse Alice"}, :refs=>[21912099, 21912097, 1079751630, 21912095, 21912093, 1110560507, 2104793864, 1079750744, 21912089, 1110560528, 21913657]}

> pbf.relations.last[:id]
=> 2826659
```

Use #each if you want to iterate over the file until EOF. Nodes, ways and relations hashes are yielded to the block.

```ruby
pbf.each do |nodes, ways, relations|
  unless nodes.empty?
    nodes.each do |node|
      # do some stuff
    end
  end
end
```

### Random access

Instead of moving sequentially through the file, you can also use #seek to jump to a given OSMData block. First,
use #size to show the number of OSMData blocks in the file:

```ruby
> pbf.size
=> 380
```

You can then seek to block n (from 0 to 379, in this case) using #seek(n). Use #pos to show the current block
(setting #pos has the same effect as calling #seek):

```ruby
> pbf.seek(25)
=> true
> pbf.pos
=> 25
> pbf.nodes.first[:id]
=> 86079877
> pbf.pos = 30
=> 30
> pbf.nodes.first[:id]
=> 142424105
```

Using a negative number will count back from the end of the list of OSMData blocks, like when you use a negative
Array index:

```ruby
> pbf.seek(-1)
=> true
> pbf.pos
=> 379
```

If the index to #seek is out of bounds, it returns false:

```ruby
> pbf.seek(380)
=> false
```

### Additional data

The OSMHeader data is also parsed and stored:

```ruby
> pbf.header
=> {"bbox"=>{:top=>43.75169, :right=>7.448637000000001, :bottom=>43.72335, :left=>7.409205}, "required_features"=>["OsmSchema-V0.6", "DenseNodes"], "optional_features"=>nil, "writing_program"=>"Osmium (http://wiki.openstreetmap.org/wiki/Osmium)", "source"=>nil, "osmosis_replication_timestamp"=>1375470002, "osmosis_replication_sequence_number"=>nil, "osmosis_replication_base_url"=>nil}
```

Also, you can examine the Array of entries describing the OSMData blobs found during the initial scan (mainly for
debugging purposes). CAUTION: this data structure should not be modified.

```ruby
> pbf.blobs
=> [{:header_pos=>142, :header_size=>13, :data_pos=>155, :data_size=>114068}, ...]
```

Whenever something goes wrong an exception is raised so wrap your calls around rescue blocks at your convenience.

## @TODO
- [ ] Write some tests
- [ ] Improve error handling

## Contributing

1. Fork it
2. Create your feature branch (`git checkout -b my-new-feature`)
3. Commit your changes (`git commit -am 'Add some feature'`)
4. Push to the branch (`git push origin my-new-feature`)
5. Create new Pull Request

## Credits

* [scrosby](https://github.com/scrosby) - PBF author
* [Osm2pgsql dev team](https://github.com/openstreetmap/osm2pgsql/graphs/contributors) - for its [parse-pbc.c](https://github.com/openstreetmap/osm2pgsql/blob/master/parse-pbf.c) file that helped me to understand the format
