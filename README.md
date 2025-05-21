# libcgraph (Incidence-Type-RePair)

**Autor:** FR, EA

libcgraph is a C-library to compress and search in labeled graphs and labeled hypergraphs.

The library should run on Linux and macOS, it is not supported on Windows.

If you have questions regarding the implementation, feel free to contact adlerenno.

## Dependencies

- [libdivsufsort](https://github.com/y-256/libdivsufsort) to create the suffix-array
  - Using Homebrew
    - brew install libdivsufsort
  - Using bash:
    - git clone https://github.com/y-256/libdivsufsort.git
    - cd libdivsufsort
    - mkdir build
    - cd build
    - cmake -DCMAKE_BUILD_TYPE="Release" -DCMAKE_INSTALL_PREFIX="/usr/local" ..
    - make
    - sudo make install
    
- [serd](https://github.com/drobilla/serd) needed by the command-line-tool to read RDF-graphs
  - Using homebrew
    - brew install serd
  - Using apt-get
    - sudo apt install libserd-dev
      - (sudo apt-get install libserd-0-0 does not work)

- [libmicrohttpd]() needed by the webservice for the server
  - Using homebrew
    - brew install libmicrohttpd
  - Using apt-get
    - sudo apt-get install libmicrohttpd-dev

## Build

To build, you need `cmake` and you can compile the library by:

```bash
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DOPTIMIZE_FOR_NATIVE=on -DWITH_RRR=on ..
make
```

The following parameters can be passed to CMake:

- `-DCMAKE_BUILD_TYPE=Release` activates compiler optimizations
- `-DOPTIMIZE_FOR_NATIVE=on` activates optimized processor functions, for example the more efficient `popcnt`-variants
- `-DNO_MMAP=on` read files using `read`-system-calls instead of `mmap`
- `-DWITH_RRR=on` activates the support for RRR bitsequences, but increases the library size significantly due to static tables. 
- `-DCLI=on` activates the compilation of the command-line-tool.
- `-DWEB_SERVICE=on` activates the compilation of the web-service. Needs the command-line-tool.

The library will be in the build-directory as "libcgraph.1.0.0.dylib" (macOS) or "libcgraph.so.1.0.0" (Linux).
The command-line-tool is in the build-directory as well and is called "cgraph-cli".

## Command-Line-Tool

With the command-line-tool "cgraph-cli" you can compress graphs and search within them.
The following help can also be obtained by `./cgraph-cli --help`.

```
Usage: cgraph-cli
    -h,--help                       show this help

 * to compress a RDF graph:
   cgraph-cli [options] [input] [output]
                       [input]          input file of the RDF graph
                       [output]         output file of the compressed graph

   optional options:
    -f,--format        [format]         format of the RDF graph; keep empty to auto detect the format
                                        possible values: "turtle", "ntriples", "nquads", "trig", "hyperedge", "cornell_hypergraph"
       --overwrite                      overwrite if the output file exists
    -v,--verbose                        print advanced information

   options to influence the resulting size and the runtime to browse the graph (optional):
       --max-rank      [rank]           maximum rank of edges, set to 0 to remove limit (default: 128)
       --monograms                      enable the replacement of monograms (default: false)
       --factor        [factor]         number of blocks of a bit sequence that are grouped into a superblock (default: 64)
       --sampling      [sampling]       sampling value of the dictionary; a value of 0 disables sampling (default: 0)
       --no-rle                         disable run-length encoding
       --no-table                       do not add an extra table to speed up the decompression of the neighborhood for an specific label
       --rrr                            use bitsequences based on R. Raman, V. Raman, and S. S. Rao [experimental] (needs -DWITH_RRR=on)
                                        --factor is also applied to this type of bit sequences


 * to read a compressed RDF graph:
   cgraph-cli [options] [input] [commands...]
                       [input]          input file of the compressed RDF graph

   optional options:
    -f,--format        [format]         default format for the RDF graph at the command `--decompress`
                                        possible values: "turtle", "ntriples", "nquads", "trig", "hyperedge"
       --overwrite                      overwrite if the output file exists, used with `--decompress`

   commands to read the compressed path:
       --decompress    [output]      decompresses the given compressed RDF graph
       --extract-node  [node-id]        extracts the node label of the given node id
       --extract-edge  [edge-id]        extracts the edge label of the given edge id
       --locate-node   [text]           determines the node id of the node with the given node label
       --locate-edge   [text]           determines the edge label id for the given text
       --locatep-node  [text]           determines the node ids that have labels starting with the given text
       --search-node   [text]           determines the node ids with labels containing the given text
       --hyperedges    [rank,label]*{,node}
                                        determines the edges with given rank. You can specify any number of nodes
                                        that will be checked the edge is connected to. The incidence-type is given 
                                        implicitly. The label must not be set, use ? otherwise. For example:
                                        - "4,2,?,3,?,4": determines all rank 4 edges with label 2 that are connected
                                           to the node 3 with connection-type 2 and node 4 with connection-type 4.
                                        - "2,?,?,5": determines all rank 2 edges any label that are connected
                                          to the node 5 with connection-type 1. In the sense of regular edges, 
                                          this asks for all incoming edges of node 5.
                                        Note that it is not allowed to pass no label and no nodes to this function.
                                        Use --decompress in this case.
       --node-count                     returns the number of nodes in the graph
       --edge-labels                    returns the number of different edge labels in the graph
       --port          [port]           starts the web-service at the given port. Webserver can be queried via easy SPARQL. (needs -DWEB_SERVICE=on)
       "
```

The command-line-tool allows via serd the Turtle, TriG, NTriples and NQuads formats for RDF-graphs. 

Additionally, there is a parser for hypergraphs. 
The formatting of a hypergraph file is one edge per line, 
each line starts with the label of the edge and is followed by the name of the nodes. 
The elements in a line can be separated by a white space or a tab. 
Note that the nodes are treated as named, 
so numbers would be inserted as names into the dictionary and the internally used numbers can differ.

A more flexibel parser for 'cornell' hypergraphs exists designed for the [hypergraphs with labeled nodes from cs.cornell.edu](https://www.cs.cornell.edu/~arb/data/).
There are three mandatory files: The hyperedges- file contains comma seperated lists of node IDs, one hyperedge per line.
The label-names- file takes contains the string labels of the node classes.
The node-labels- file contains one integer per line assigning one class to each node. The classes are represented by rank-1 edges by ITR.
Optionally, the node-names- file, which contains a unique node name per node.
To properly call this, you need to give the filename without any prefix, so for example contact-primary-school.txt and ITR will find the files accordingly.
There are some important things to know for querying the generated file:
First, the node classes start at 0, so the rank-1 edges for the node classes have lower edge labels assigned by ITR than the hyperedges. 
This is due to the fact that the hyperedges do not have edge labels using this parser. 
All hyperedges of the same rank have the same label, different ranks have different labels. 
The labels follow consequently by size after the rank-1 edges.

## Library

To use Incidence-Type-RePair as a library, in the include folder is the corresponding header file with the methods supported by the library.

## SPARQL Webservice

Given a compressed file F, start the webserver for that file using ```--port 8080```. 
The value ```0``` can be used to let the system decide, which port it should use.
Both GET and POST are supported for Queries. An example GET Query is:

```http://localhost:8080/?query=SELECT%20?s%20?p%20?o%20WHERE%20{%20?s%20?p%2004%20.%20}```

The Syntax very strict for the query ```SELECT ?s ?p ?o WHERE { ?s ?p 04 . }```: 
The first 6 letters need to be ```SELECT```. If you write the Query in quotation marks, it will fail. 
If there is no ```WHERE``` or no brackets, it will fail. 
The ```?s``` terms after ```SELECT``` determine only, if it will be part of the output, not the order, so ```?p ?o ?s``` will be output still as S P O order.
If only ```?p ?o``` is given, it will only output P and O.
In the WHERE, there is exactly one triple allowed. A value can be set, like 04, or unbound. 
In the second case, it must be a question mark followed by the s, p or o, depending on what is unbound.

Failures are always result in a no return by HTTPS.

In all successful cases, the webserver returns a JSON-File containing the (possibly empty) answer to the query.

On the jamendo dataset, the above query will return

```{ "head": { "vars": [ "s", "p", "o" ] }, "results": { "bindings": [{"s": http://dbtune.org/jamendo/track/10174, "p": http://purl.org/dc/elements/1.1/title, "o": 04}, {"s": http://dbtune.org/jamendo/track/35678, "p": http://purl.org/dc/elements/1.1/title, "o": 04}] } }```

