lexer-overflow:	    (added by Matthias Andree)
---------------

Provided by Julius Plenz, FU Berlin, Germany, 2010-07-21.

Reported 2010-07-21 by Julius Plenz, after a colleague of his fuzzed
bogofilter with 18 million messages the previous week-end. These eight
cause bogofilter to exit with status 3 after reporting "flex scanner
push-back overflow".

Obtained from:
http://userpage.fu-berlin.de/~plenz/bogofilter/lexer-overflows.tar.gz

Proposed action:
- identify the culprit in the lexer ensemble - probably HTML stuff
- fix it
- integrate these into the self-test suite


Another incident reported by Seth David Schoen on 2011-05-07 to the
mailing list, who showed a sample, dubbed lexer-overflow/spam, that
breaks the RFC2047 part.  It gets trapped when the flex part of the
lexer tries to decode the MIME headers.  I know this recursion was evil,
but I hadn't expected we'd jam the lexer with pushbacks here.  Anyways,
the solution is to avoid any kind of nontrivial pushback in the lexer.
