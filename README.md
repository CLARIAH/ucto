[![Build Status](https://travis-ci.org/LanguageMachines/ucto.svg?branch=master)](https://travis-ci.org/LanguageMachines/ucto) [![Language Machines Badge](http://applejack.science.ru.nl/lamabadge.php/ucto)](http://applejack.science.ru.nl/languagemachines/)

Ucto - A rule-based tokeniser
================================

    Centre for Language and Speech technology, Radboud University Nijmegen
    Induction of Linguistic Knowledge Research Group, Tilburg University

Website: https://languagemachines.github.io/ucto/

Ucto tokenizes text files: it separates words from punctuation, and splits
sentences. This is one of the first tasks for almost any Natural Language
Processing application. Ucto offers several other basic preprocessing steps
such as changing case that you can all use to make your text suited for further
processing such as indexing, part-of-speech tagging, or machine translation.

Ucto comes with tokenisation rules for several languages (packaged separately)
and can be easily extended to suit other languages. It has been incorporated
for tokenizing Dutch text in Frog (https://languagemachines.github.io/frog),
our Dutch morpho-syntactic processor.

The software is intended to be used from the command-line by researchers in
Natural Language Processing or related areas, as well as software developers.
An [Ucto python binding](https://github.com/proycon/python-ucto) is also available
separately.

Features:

- Comes with tokenization rules for English, Dutch, French, Italian, Turkish,
  Spanish, Portuguese and Swedish; easily extendible to other languages. Rules
  consists of regular expressions and lists. They are
  packaged separately as [uctodata](https://github.com/LanguageMachines/uctodata).
- Recognizes units, currencies, abbreviations, and simple dates and times like dd-mm-yyyy
- Recognizes paired quote spans, sentences, and paragraphs.
- Produces UTF8 encoding and NFC output normalization, optionally accepting
  other input encodings as well.
- Ligature normalization (can undo for isntance fi,fl as single codepoints).
- Optional conversion to all lowercase or uppercase.
- Supports [FoLiA XML](https://proycon.github.io/folia)

Ucto was written by Maarten van Gompel and Ko van der Sloot. Work on Ucto was
funded by NWO, the Netherlands Organisation for Scientific Research, under the
Implicit Linguistics project, the CLARIN-NL program, and the CLARIAH project.

This software is available under the GNU Public License v3 (see the file
COPYING).

Installation
------------------------------------------------------------

To install ucto, first consult whether your distribution's package manager has an up-to-date package for it.
If not, for easy installation of ucto and all dependencies, it is included as part of our software
distribution [LaMachine](https://proycon.github.io/LaMachine).

To compile and install manually from source, provided you have all the
dependencies installed:

    $ bash bootstrap.sh
    $ ./configure
    $ make
    $ sudo make install

You will need current versions of the following dependencies of our software:

* [ticcutils](https://github.com/LanguageMachine/ticcutils) - A shared utility library
* [libfolia](https://github.com/LanguageMachines/libfolia)  - A library for the FoLiA format.
* [uctodata](https://github.com/LanguageMachines/uctodata)  - Data files for ucto, packaged separately

As well as the following 3rd party dependencies:

* ``icu`` - A C++ library for Unicode and Globalization support. On Debian/Ubuntu systems, install the package libicu-dev.
* ``libxml2`` - An XML library. On Debian/Ubuntu systems install the package libxml2-dev.
* A sane build environment with a C++ compiler (e.g. gcc or clang), autotools, libtool, pkg-config

Usage
------------------------------------------------------------

Tokenize an english text file to standard output, tokens will be
space-seperated, sentences delimiter by ``<utt>``:

    $ ucto -L eng yourfile.txt

The -L flag specifies the language (as a three letter iso-639-3 code), provided
a configuration file exists for that language. The configurations are provided
separately, for various languages, in the
[uctodata](https://github.com/LanguageMachines/uctodata) package. Note that
older versions of ucto used different two-letter codes, so you may need to
update the way you invoke ucto.

To output to file instead of standard output, just add another
positional argument with the desired output filename.

If you want each sentence on a separate line (i.e. newline delimited rather than delimited by
``<utt>``), then pass the ``-n`` flag. If each sentence is already on one line
in the input and you want to leave it at that, pass the ``-m`` flag.

Tokenize plaintext to [FoLiA XML](https://proycon.github.io/folia) using the ``-X`` flag, you can specify an ID
for the FoLiA document using the ``--id=`` flag.

    $ ucto -L eng -X --id=hamlet hamlet.txt hamlet.folia.xml

Note that in the FoLiA XML output, ucto encodes the class of the token (date, url, smiley, etc...) based
on the rule that matched.

For further documentation consult the [ucto
manual](https://github.com/LanguageMachines/ucto/blob/master/docs/ucto_manual.pdf)
for further documentation.



