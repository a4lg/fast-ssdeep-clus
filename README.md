fast-ssdeep-clus : Parallel ssdeep clustering kit
==================================================


What is this?
--------------

This is simple and efficient ssdeep-based clustering program.
I use this program to cluster my malware collection
(based on certain threshold value).


Requirements
-------------

*	ffuzzy++ 4.0  
	<https://github.com/a4lg/ffuzzypp>

There's no Makefile because this entire program is a hack.
Please build it yourself.


File Format : Fuzzy hash list
------------------------------

This is simple fuzzy hash list separated by newline character.

	3::
	24:VGXGP7L5e/Ixt3af/WKPPaYpzg4m3XWMCsXNCRs0:kYDxcfPZpelCs9Cm0
	24:3G24hjRrxvF61IJG1XNgf+ojDOja9/JTjw1UBr2mk:W5FhnVtajaRG1gr2r
	(... continues without empty lines ...)
	(EOF)


File Format : Cluster list
---------------------------

It indicates which fuzzy hashes are connected.
If fuzzy hash `A` and `B` are connected and `C`, `D`, `E` are connected,
the cluster list should look like this:

	A
	B
	(empty line)
	C
	D
	E
	(empty line)
	(EOF)

There is always an empty line after each cluster to indicate
end of cluster.


Program : fast-ssdeep-clus
---------------------------

*	Input:  fuzzy hash list (as first argument)
*	Output: cluster list (to `stdout`)

This program clusters fuzzy hashes and makes cluster list.


Program : fast-combine-ssdeep-clus
-----------------------------------

*	Input 1: fuzzy hash list (as first argument)
*	Input 2: fuzzy hash list (as second argument)
*	Output:  cluster list (to `stdout`)

This program tries to make clusters between elements in Input 1 and
elements in Input 2. This program is useful for making clusters
gradually.


Program : combine-clusters
---------------------------

*	Input:  cluster list (from `stdin`)
*	Output: cluster list (to `stdout`)

This program combines clusters. For instance, if input like this
is given...:

	A
	B
	(empty line)
	C
	D
	E
	(empty line)
	B
	C
	(empty line)
	(EOF)

the output should look like this:

	A
	B
	C
	D
	E
	(empty line)
	(EOF)

Because all five elements are connected (indirectly), this program
combines all connected clusters into a single cluster.


Program : sort-clusters
------------------------

*	Input:  cluster list (from `stdin`)
*	Output: cluster list (to `stdout`)

This program normalizes output by sorting cluster list on certain order.


Usage
------

```
# threshold = 79, number of threads = 8

# to make cluster list
$ fast-ssdeep-clus -t 79 -n 8 all.ssdeep | sort-clusters >all.clusters.79.txt

# to make clusters gradually
$ fast-ssdeep-clus -t 79 -n 8 delta.ssdeep >delta.clusters.79.0
$ fast-combine-ssdeep-clus -t 79 -n 8 delta.ssdeep all.ssdeep >delta.clusters.79.1
$ cat all.clusters.79.txt delta.clusters.79.0 delta.clusters.79.1 \
	| combine-clusters | sort-clusters >all.clusters.79.txt.new
$ mv all.clusters.79.txt.new all.clusters.79.txt
```


License
--------

This program itself is released under the terms of the ISC License.
However, ffuzzy++ is released under the terms of GNU General Public
License (GNU GPL). See license files of ffuzzy++ for details.
