/*
  Copyright (c) 2006 - 2018
  CLST - Radboud University
  ILK  - Tilburg University

  This file is part of Ucto

  Ucto is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  Ucto is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  For questions and suggestions, see:
      https://github.com/LanguageMachines/ucto/issues
  or send mail to:
      lamasoftware (at ) science.ru.nl

*/

#include "ucto/tokenize.h"

#include <unistd.h>
#include <iostream>
#include <fstream>
#include <vector>
#include "config.h"
#include "unicode/schriter.h"
#include "unicode/ucnv.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/PrettyPrint.h"
#include "ticcutils/Unicode.h"
#include "ucto/my_textcat.h"

#define DO_READLINE
#ifdef HAVE_LIBREADLINE
#  if defined(HAVE_READLINE_READLINE_H)
#    include <readline/readline.h>
#  elif defined(HAVE_READLINE_H)
#    include <readline.h>
#  else
#    undef DO_READLINE
#  endif /* !defined(HAVE_READLINE_H) */
#else
#  undef DO_READLINE
#endif /* HAVE_LIBREADLINE */

#ifdef HAVE_READLINE_HISTORY
#  if defined(HAVE_READLINE_HISTORY_H)
#    include <readline/history.h>
#  elif defined(HAVE_HISTORY_H)
#    include <history.h>
#  endif /* defined(HAVE_READLINE_HISTORY_H) */
#endif /* HAVE_READLINE_HISTORY */

using namespace std;

#define LOG *TiCC::Log(theErrLog)

namespace Tokenizer {

  const string ISO_SET = "http://raw.github.com/proycon/folia/master/setdefinitions/iso639_3.foliaset";

  std::string Version() { return VERSION; }
  std::string VersionName() { return PACKAGE_STRING; }

  class uRangeError: public std::out_of_range {
  public:
    explicit uRangeError( const string& s ): out_of_range( "ucto: out of range:" + s ){};
  };

  class uLogicError: public std::logic_error {
  public:
    explicit uLogicError( const string& s ): logic_error( "ucto: logic error:" + s ){};
  };

  class uCodingError: public std::runtime_error {
  public:
    explicit uCodingError( const string& s ): runtime_error( "ucto: coding problem:" + s ){};
  };


  icu::UnicodeString convert( const string& line,
			 const string& inputEncoding ){
    icu::UnicodeString result;
    try {
      result = icu::UnicodeString( line.c_str(),
			      line.length(),
			      inputEncoding.c_str() );
    }
    catch ( exception &e) {
      throw uCodingError( "Unexpected character found in input. " +
			  string(e.what()) + "Make sure input is valid: " +
			  inputEncoding );
    }
    if ( result.isBogus() ){
      throw uCodingError( "string decoding failed: (invalid inputEncoding '"
			  + inputEncoding + "' ?)" );
    }
    return result;
  }

  const icu::UnicodeString type_space = "SPACE";
  const icu::UnicodeString type_currency = "CURRENCY";
  const icu::UnicodeString type_emoticon = "EMOTICON";
  const icu::UnicodeString type_picto = "PICTOGRAM";
  const icu::UnicodeString type_word = "WORD";
  const icu::UnicodeString type_symbol = "SYMBOL";
  const icu::UnicodeString type_punctuation = "PUNCTUATION";
  const icu::UnicodeString type_number = "NUMBER";
  const icu::UnicodeString type_unknown = "UNKNOWN";

  Token::Token( const icu::UnicodeString& _type,
		const icu::UnicodeString& _s,
		TokenRole _role, const string& _lc ):
    type(_type), us(_s), role(_role), lc(_lc) {
    //    cerr << "Created " << *this << endl;
  }


  std::string Token::texttostring() { return TiCC::UnicodeToUTF8(us); }
  std::string Token::typetostring() { return TiCC::UnicodeToUTF8(type); }

  ostream& operator<< (std::ostream& os, const Token& t ){
    os << t.type << " : " << t.role  << ":" << t.us;
    return os;
  }

  ostream& operator<<( ostream& os, const TokenRole& tok ){
    if ( tok & NOSPACE) os << "NOSPACE ";
    if ( tok & BEGINOFSENTENCE) os << "BEGINOFSENTENCE ";
    if ( tok & ENDOFSENTENCE) os << "ENDOFSENTENCE ";
    if ( tok & NEWPARAGRAPH) os << "NEWPARAGRAPH ";
    if ( tok & BEGINQUOTE) os << "BEGINQUOTE ";
    if ( tok & ENDQUOTE) os << "ENDQUOTE ";
    return os;
  }

  TokenizerClass::TokenizerClass():
    linenum(0),
    inputEncoding( "UTF-8" ),
    eosmark("<utt>"),
    tokDebug(0),
    verbose(false),
    detectBounds(true),
    detectQuotes(false),
    doFilter(true),
    doPunctFilter(false),
    detectPar(true),
    paragraphsignal(true),
    doDetectLang(false),
    text_redundancy("minimal"),
    sentenceperlineoutput(false),
    sentenceperlineinput(false),
    lowercase(false),
    uppercase(false),
    xmlout(false),
    xmlin(false),
    passthru(false),
    inputclass("current"),
    outputclass("current"),
    tc( 0 )
  {
    theErrLog = new TiCC::LogStream(cerr, "ucto" );
    theErrLog->setstamp( StampMessage );
#ifdef HAVE_TEXTCAT
    string textcat_cfg = string(SYSCONF_PATH) + "/ucto/textcat.cfg";
    tc = new TextCat( textcat_cfg );
#endif
  }

  TokenizerClass::~TokenizerClass(){
    Setting *d = 0;
    for ( const auto& s : settings ){
      if ( s.first == "default" ){
	// the 'default' may also return as a real 'language'
	// avoud delettng it twice
	d = s.second;
	delete d;
      }
      if ( s.second != d ){
	delete s.second;
      }

    }
    delete theErrLog;
    delete tc;
  }

  bool TokenizerClass::reset( const string& lang ){
    tokens.clear();
    settings[lang]->quotes.clearStack();
    return true;
  }

  bool TokenizerClass::setNormSet( const std::string& values ){
    vector<string> parts;
    TiCC::split_at( values, parts, "," );
    for ( const auto& val : parts ){
      norm_set.insert( TiCC::UnicodeFromUTF8( val ) );
    }
    return true;
  }

  void TokenizerClass::setErrorLog( TiCC::LogStream *os ) {
    if ( theErrLog != os ){
      delete theErrLog;
    }
    theErrLog = os;
  }

  string TokenizerClass::setInputEncoding( const std::string& enc ){
    string old = inputEncoding;
    inputEncoding = enc;
    return old;
  }

  string TokenizerClass::setTextRedundancy( const std::string& tr ){
    if ( tr == "none" || tr == "minimal" || tr == "full" ){
      string s = text_redundancy;
      text_redundancy = tr;
      return s;
    }
    else {
      throw runtime_error( "illegal value '" + tr + "' for textredundancy. "
			   "expected 'full', 'minimal' or 'none'." );
    }
  }

  void stripCR( string& s ){
    string::size_type pos = s.rfind( '\r' );
    if ( pos != string::npos ){
      s.erase( pos );
    }
  }

  void TokenizerClass::extractSentencesAndFlush( int numS,
						 vector<Token>& outputTokens,
						 const string& lang ){
    int count = 0;
    const int size = tokens.size();
    short quotelevel = 0;
    size_t begin = 0;
    size_t end = 0;
    for ( int i = 0; i < size; ++i ) {
      if (tokens[i].role & NEWPARAGRAPH) {
	quotelevel = 0;
      }
      else if (tokens[i].role & ENDQUOTE) {
	--quotelevel;
      }
      if ( (tokens[i].role & BEGINOFSENTENCE)
	   && (quotelevel == 0)) {
	begin = i;
      }
      //FBK: QUOTELEVEL GOES UP BEFORE begin IS UPDATED... RESULTS IN DUPLICATE OUTPUT
      if (tokens[i].role & BEGINQUOTE) {
	++quotelevel;
      }
      if ((tokens[i].role & ENDOFSENTENCE) && (quotelevel == 0)) {
	end = i+1;
	tokens[begin].role |= BEGINOFSENTENCE;  //sanity check
	if (tokDebug >= 1){
	  LOG << "[tokenize] extracted sentence " << count << ", begin="<<begin << ",end="<< end << endl;
	}
	for ( size_t i=begin; i < end; ++i ){
	  outputTokens.push_back( tokens[i] );
	}
	if ( ++count == numS ){
	  if (tokDebug >= 1){
	    LOG << "[tokenize] erase " << end  << " tokens from " << tokens.size() << endl;
	  }
	  tokens.erase( tokens.begin(),tokens.begin()+end );
	  if ( !passthru ){
	    if ( !settings[lang]->quotes.emptyStack() ) {
	      settings[lang]->quotes.flushStack( end );
	    }
	  }
	  //After flushing, the first token still in buffer (if any) is always a BEGINOFSENTENCE:
	  if (!tokens.empty()) {
	    tokens[0].role |= BEGINOFSENTENCE;
	  }
	  return;
	}
      }
    }
    if ( count < numS ){
      throw uRangeError( "Not enough sentences exists in the buffer: ("
			 + toString( count ) + " found. " + toString( numS)
			 + " wanted)" );
    }
  }

  vector<Token> TokenizerClass::tokenizeStream( istream& IN,
						const string& lang ) {
    vector<Token> outputTokens;
    bool done = false;
    bool bos = true;
    do {
      string line;
      done = !getline( IN, line );
      ++linenum;
      if ( tokDebug > 0 ){
	LOG << "[tokenize] Read input line # " << linenum
			<< "\nline:'" << TiCC::format_nonascii( line )
			<< "'" << endl;
      }
      stripCR( line );
      icu::UnicodeString input_line;
      if ( line.size() > 0 && line[0] == 0 ){
	// when processing UTF16LE, '0' bytes show up at pos 0
	// we discard them, not for UTF16BE!
	// this works on Linux with GCC (atm)
	if ( inputEncoding != "UTF16BE" ){
	  line.erase(0,1);
	}
      }
      if ( line.size() > 0 && inputEncoding == "UTF16BE" &&
	   line.back() == 0 ){
	// when processing UTF16BE, '0' bytes show up at the end
	// we discard them.
	// this works on Linux with GCC (atm)
	line.erase(line.size()-1);
      }
      if ( !line.empty() ){
	if ( tokDebug > 0 ){
	  LOG << "voor strip:'" << TiCC::format_nonascii( line ) << "'" << endl;
	}
	input_line = convert( line, inputEncoding );
	if ( sentenceperlineinput ){
	  input_line += " " + eosmark;
	}
      }
      else {
	if ( sentenceperlineinput ){
	  input_line = eosmark;
	}
      }
      int numS;
      if ( done
	   || input_line.isEmpty() ){
	signalParagraph();
	numS = countSentences(true); //count full sentences in token buffer, force buffer to empty!
      }
      else {
	if ( passthru ){
	  passthruLine( input_line, bos );
	}
	else {
	  string language;
	  if ( tc ){
	    if ( tokDebug > 3 ){
	      LOG << "use textCat to guess language from: "
		  << input_line << endl;
	    }
	    icu::UnicodeString temp = input_line;
	    temp.toLower();
	    string lan = tc->get_language( TiCC::UnicodeToUTF8(temp) );
	    if ( settings.find( lan ) != settings.end() ){
	      if ( tokDebug > 3 ){
		LOG << "found a supported language: " << lan << endl;
	      }
	    }
	    else {
	      if ( tokDebug > 3 ){
		LOG << "found an unsupported language: " << lan << endl;
	      }
	      lan = "default";
	    }
	    language = lan;
	  }
	  tokenizeLine( input_line, language, "" );
	}
	numS = countSentences(); //count full sentences in token buffer
      }
      if ( numS > 0 ) { //process sentences
	if ( tokDebug > 0 ){
	  LOG << "[tokenize] " << numS << " sentence(s) in buffer, processing..." << endl;
	}
	extractSentencesAndFlush( numS, outputTokens, lang );
	return outputTokens;
      }
      else {
	if  (tokDebug > 0) {
	  LOG << "[tokenize] No sentences yet, reading on..." << endl;
	}
      }
    } while (!done);
    return outputTokens;
  }

  string TokenizerClass::tokenizeSentenceStream( istream& IN,
						 const string& lang ) {
    string result;
    int numS = countSentences(); //count full sentences in token buffer
    if ( numS > 0 ) { // still some sentences in the buffer
      if  (tokDebug > 0) {
	LOG << "[tokenizeStream] " << numS
			<< " sentence(s) in buffer, processing..." << endl;
      }
      result = getSentenceString( 0 );
      // clear processed sentence from buffer
      if  (tokDebug > 0){
	LOG << "[tokenizeStream] flushing 1 "
			<< " sentence from buffer..." << endl;
      }
      flushSentences( 1, lang );
      return result;
    }
    bool done = false;
    bool bos = true;
    string line;
    do {
      done = !getline( IN, line );
      linenum++;
      if (tokDebug > 0) {
	LOG << "[tokenize] Read input line " << linenum << endl;
      }
      stripCR( line );
      if ( sentenceperlineinput )
	line += string(" ") + TiCC::UnicodeToUTF8(eosmark);
      if ( (done) || (line.empty()) ){
	signalParagraph();
	numS = countSentences(true); //count full sentences in token buffer, force buffer to empty!
      }
      else {
	if ( passthru )
	  passthruLine( line, bos );
	else
	  tokenizeLine( line, lang );
	numS = countSentences(); //count full sentences in token buffer
      }
      if ( numS > 0 ) {
	// 1 or more sentences in the buffer.
	// extract the first 1
	if  (tokDebug > 0) {
	  LOG << "[tokenizeStream] " << numS << " sentence(s) in buffer, processing first one..." << endl;
	}
	result = getSentenceString( 0 );
	//clear processed sentence from buffer
	if  (tokDebug > 0){
	  LOG << "[tokenizeStream] flushing 1 "
			  << " sentence(s) from buffer..." << endl;
	}
	flushSentences( 1, lang );
	return result;
      }
      else {
	if  (tokDebug > 0) {
	  LOG << "[tokenizeStream] No sentence yet, reading on..." << endl;
	}
      }
    } while (!done);
    return result;
  }

  folia::Document *TokenizerClass::tokenize( istream& IN ) {
    inputEncoding = checkBOM( IN );
    folia::Document *doc = new folia::Document( "id='" + docid + "'" );
    if ( /*doDetectLang &&*/ default_language != "none" ){
      if ( tokDebug > 0 ){
	LOG << "[tokenize](stream): SET document language=" << default_language << endl;
      }
      doc->set_metadata( "language", default_language );
    }
    outputTokensDoc_init( *doc );
    folia::FoliaElement *root = doc->doc()->index(0);
    int parCount = 0;
    vector<Token> buffer;
    do {
      if ( tokDebug > 0 ){
	LOG << "[tokenize] looping on stream" << endl;
      }
      vector<Token> v = tokenizeStream( IN );
      for ( auto const& token : v ) {
	if ( token.role & NEWPARAGRAPH) {
	  //process the buffer
	  parCount = outputTokensXML( root, buffer, parCount );
	  buffer.clear();
	}
	buffer.push_back( token );
      }
    }
    while ( IN );
    if ( tokDebug > 0 ){
      LOG << "[tokenize] end of stream reached" << endl;
    }
    if (!buffer.empty()){
      outputTokensXML( root, buffer, parCount);
    }
    return doc;
  }

  void TokenizerClass::tokenize( const string& ifile, const string& ofile) {
    ostream *OUT = NULL;
    if ( ofile.empty() )
      OUT = &cout;
    else {
      OUT = new ofstream( ofile );
    }

    istream *IN = NULL;
    if (!xmlin) {
      if ( ifile.empty() )
	IN = &cin;
      else {
	IN = new ifstream( ifile );
	if ( !IN || !IN->good() ){
	  cerr << "ucto: problems opening inputfile " << ifile << endl;
	  cerr << "ucto: Courageously refusing to start..."  << endl;
	  throw runtime_error( "unable to find or read file: '" + ifile + "'" );
	}
      }
      this->tokenize( *IN, *OUT );
    }
    else {
      folia::Document doc;
      doc.readFromFile(ifile);
      if ( xmlin && inputclass == outputclass ){
	LOG << "ucto: --filter=NO is automatically set. inputclass equals outputclass!"
	    << endl;
	setFiltering(false);
      }
      this->tokenize(doc);
      *OUT << doc << endl;
    }

    if ( IN != &cin ) delete IN;
    if ( OUT != &cout ) delete OUT;
  }

  void TokenizerClass::tokenize( istream& IN, ostream& OUT) {
    if (xmlout) {
      folia::Document *doc = tokenize( IN );
      OUT << doc << endl;
      delete doc;
    }
#ifdef DO_READLINE
    else if ( &IN == &cin && isatty(0) ){
      // interactive use on a terminal (quite a hack..)
      const char *prompt = "ucto> ";
      string line;
      int i = 0;
      while ( true ){
	string data;
	char *input = readline( prompt );
	if ( !input ){
	  break;
	}
	line = input;
	sentenceperlineinput = true;
	if ( line.empty() ){
	  free( input );
	  continue;
	}
	else {
	  add_history( input );
	  free( input );
	  data += line + " ";
	}
	if ( !data.empty() ){
	  istringstream inputstream(data,istringstream::in);
	  vector<Token> v = tokenizeStream( inputstream );
	  if ( !v.empty() ) {
	    outputTokens( OUT, v, (i>0) );
	  }
	  ++i;
	  OUT << endl;
	}
      }
    }
#endif
    else {
      int i = 0;
      inputEncoding = checkBOM( IN );
      do {
	if ( tokDebug > 0 ){
	  LOG << "[tokenize] looping on stream" << endl;
	}
	vector<Token> v = tokenizeStream( IN );
	if ( !v.empty() ) {
	  outputTokens( OUT, v , (i>0) );
	}
	++i;
      } while ( IN );
      if ( tokDebug > 0 ){
	LOG << "[tokenize] end_of_stream" << endl;
      }
      OUT << endl;
    }
  }

  bool TokenizerClass::tokenize( folia::Document& doc ) {
    if ( tokDebug >= 2 ){
      LOG << "tokenize doc " << doc << endl;
    }
    if ( xmlin && inputclass == outputclass ){
      LOG << "ucto: --filter=NO is automatically set. inputclass equals outputclass!"
	  << endl;
      setFiltering(false);
    }
    if ( true /*doDetectLang*/ ){
      string lan = doc.doc()->language();
      if ( lan.empty() && default_language != "none" ){
	if ( tokDebug > 1 ){
	  LOG << "[tokenize](FoLiA) SET document language=" << default_language << endl;
	}
	if ( doc.metadatatype() == "native" ){
	  doc.set_metadata( "language", default_language );
	}
	else {
	  LOG << "[WARNING] cannot set the language on FoLiA documents of type "
	      << doc.metadatatype() << endl;
	}
      }
      else {
	if ( tokDebug >= 2 ){
	  LOG << "[tokenize](FoLiA) Document has language " << lan << endl;
	}
      }
    }
    for ( size_t i = 0; i < doc.doc()->size(); i++) {
      if (tokDebug >= 2) {
	LOG << "[tokenize] Invoking processing of first-level element " << doc.doc()->index(i)->id() << endl;
      }
      tokenizeElement( doc.doc()->index(i) );
    }
    return true;
  }

  void appendText( folia::FoliaElement *root,
		   const string& outputclass  ){
    // set the textcontent of root to that of it's children
    if ( root->hastext( outputclass ) ){
      // there is already text, bail out.
      return;
    }
    if ( root->isSubClass( folia::Linebreak_t ) ){
      // exception
      return;
    }
    icu::UnicodeString utxt = root->text( outputclass, false, false );
    // so get Untokenized text from the children, and set it
    root->settext( TiCC::UnicodeToUTF8(utxt), outputclass );
  }

  void removeText( folia::FoliaElement *root,
		   const string& outputclass  ){
    // remove the textcontent in outputclass of root
    root->cleartextcontent( outputclass );
  }

  const string get_language( folia::FoliaElement *f ) {
    // get the language of this element, if any, don't look up.
    // we search in ALL possible sets!
    string st = "";
    std::set<folia::ElementType> exclude;
    vector<folia::LangAnnotation*> v
      = f->select<folia::LangAnnotation>( st, exclude, false );
    string result;
    if ( v.size() > 0 ){
      result = v[0]->cls();
    }
    return result;
  }

  void set_language( folia::FoliaElement* e, const string& lan ){
    // set or reset the language: append a LangAnnotation child of class 'lan'
    folia::KWargs args;
    args["class"] = lan;
    args["set"] = ISO_SET;
    folia::LangAnnotation *node = new folia::LangAnnotation( e->doc() );
    node->setAttributes( args );
    e->replace( node );
  }

  void TokenizerClass::tokenizeElement( folia::FoliaElement * element) {
    if ( element->isinstance(folia::Word_t)
	 || element->isinstance(folia::TextContent_t))
      // shortcut
      return;
    if ( tokDebug >= 2 ){
      LOG << "[tokenizeElement] Processing FoLiA element " << element->xmltag()
	  << "(" << element->id() << ")" << endl;
      LOG << "[tokenizeElement] inputclass=" << inputclass << " outputclass=" << outputclass << endl;
    }
    if ( element->hastext( inputclass ) ) {
      // We have an element which contains text. That's nice
      // now we must see wether some 'formatting' is there. ( like Words() or
      // Sentences() )
      // If so: assume that the text is tokenized already, and don't spoil that
      if ( element->isinstance(folia::Paragraph_t) ) {
	//tokenize paragraph: check for absence of sentences
	vector<folia::Sentence*> sentences = element->sentences();
	if (sentences.size() > 0) {
	  // bail out
	  return;
	}
      }
      else if ( ( element->isinstance(folia::Sentence_t) )
		|| ( element->isinstance(folia::Head_t) ) ) {
	//tokenize sentence: check for absence of Word's
	vector<folia::Word*> words = element->words();
	if (words.size() > 0) {
	  // bail out
	  return;
	}
      }
      else {
	// Some other element that contains text. Probably deeper.
	// look it up. skip all paragraphs and sentences
	vector<folia::Paragraph*> paragraphs = element->paragraphs();
	if (paragraphs.size() > 0) {
	  // already paragraphs, bail out
	  return;
	}
	vector<folia::Sentence*> sentences = element->sentences();
	if (sentences.size() > 0) {
	  // already sentences, bail out
	  return;
	}
	vector<folia::Word*> words = element->words();
	if (words.size() > 0) {
	  // already words, bail out
	  return;
	}
      }
      // check feasability
      if ( inputclass != outputclass && outputclass == "current" ){
	if ( element->hastext( outputclass ) ){
	  throw uLogicError( "cannot set text with class='current' on node "
			     + element->id() +
			     " because it already has text in that class." );
	}
      }
      // now let's check our language
      string lan;
      if ( doDetectLang ){
	lan = get_language( element ); // is there a local element language?
	if ( lan.empty() ){
	  // no, so try to detect it!
	  icu::UnicodeString temp = element->text( inputclass );
	  temp.toLower();
	  lan = tc->get_language( TiCC::UnicodeToUTF8(temp) );
	  if ( lan.empty() ){
	    // too bad
	    lan = "default";
	  }
	  else {
	    if ( tokDebug >= 2 ){
	      LOG << "[tokenizeElement] textcat found a supported language: " << lan << endl;
	    }
	  }
	}
      }
      else {
	lan = element->language(); // remember thus recurses upward
	// to get a language from the node, it's parents OR the doc
	if ( lan.empty() || default_language == "none" ){
	  lan = "default";
	}
      }
      auto const it = settings.find(lan);
      if ( it != settings.end() ){
	if ( tokDebug >= 2 ){
	  LOG << "[tokenizeElement] Found a supported language: " << lan << endl;
	}
      }
      else if ( !default_language.empty() ){
	if ( default_language != lan ){
	  // skip elements in the wrong language
	  if ( tokDebug >= 2 ){
	    LOG << "[tokenizeElement] skip tokenizing because:" << lan << " isn't supported" << endl;
	  }
	  return;
	}
	else {
	  lan = "default";
	}
      }
      // so we have text, in an element without 'formatting' yet, good
      // lets Tokenize the available text!
      if ( lan != default_language
	   && lan != "default"
	   && !element->hasannotation<folia::LangAnnotation>() ){
	element->doc()->declare( folia::AnnotationType::LANG,
				 ISO_SET, "annotator='ucto'" );
	if ( tokDebug >= 2 ){
	  LOG << "[tokenizeElement] set language to " << lan << endl;
	}
	set_language( element, lan );
      }
      tokenizeSentenceElement( element, lan );
      return;
    }
    //recursion step for textless elements
    if ( tokDebug >= 2 ){
      LOG << "[tokenizeElement] Processing children of FoLiA element " << element->id() << endl;
    }
    for ( size_t i = 0; i < element->size(); i++) {
      tokenizeElement( element->index(i));
    }
    if ( text_redundancy == "full" ){
      if ( tokDebug > 0 ) {
	LOG << "[tokenizeElement] Creating text on " << element->id() << endl;
      }
      appendText( element, outputclass );
    }
    else if ( text_redundancy == "none" ){
      if ( tokDebug > 0 ) {
	LOG << "[tokenizeElement] Removing text from: " << element->id() << endl;
      }
      removeText( element, outputclass );
    }
    return;
  }

  int split_nl( const icu::UnicodeString& line,
		   vector<icu::UnicodeString>& parts ){
    static TiCC::UnicodeRegexMatcher nl_split( "\\n", "newline_splitter" );
    return nl_split.split( line, parts );
  }

  void TokenizerClass::tokenizeSentenceElement( folia::FoliaElement *element,
						const string& lang ){
    folia::Document *doc = element->doc();
    if ( passthru ){
      doc->declare( folia::AnnotationType::TOKEN, "passthru", "annotator='ucto', annotatortype='auto', datetime='now()'" );
    }
    else {
      doc->declare( folia::AnnotationType::TOKEN,
		    settings[lang]->set_file,
		    "annotator='ucto', annotatortype='auto', datetime='now()'" );
    }
    if  ( tokDebug > 0 ){
      LOG << "[tokenizeSentenceElement] " << element->id() << endl;
    }
    icu::UnicodeString line = element->stricttext( inputclass );
    if ( line.isEmpty() ){
      // so no usefull text in this element. skip it
      return;
    }
    line += " "  + eosmark;
    if ( tokDebug >= 1 ){
      LOG << "[tokenizeSentenceElement] Processing sentence:"
		      << line << endl;
    }
    if ( passthru ){
      bool bos = true;
      passthruLine( line, bos );
    }
    else {
      // folia may encode newlines. These should be converted to <br/> nodes
      // but Linebreak and newline handling is very dangerous and complicated
      // so for now is is disabled!
      vector<icu::UnicodeString> parts;
      parts.push_back( line ); // just one part
      //split_nl( line, parts ); // disabled multipart
      for ( auto const& l : parts ){
	if ( tokDebug >= 1 ){
	  LOG << "[tokenizeSentenceElement] tokenize part: " << l << endl;
	}
	tokenizeLine( l, lang, element->id() );
	if ( &l != &parts.back() ){
	  // append '<br'>
	  Token T( "type_linebreak", "\n", LINEBREAK, "" );
	  if ( tokDebug >= 1 ){
	    LOG << "[tokenizeSentenceElement] added LINEBREAK token " << endl;
	  }
	  tokens.push_back( T );
	}
      }
    }
    //ignore EOL data, we have by definition only one sentence:
    int numS = countSentences(true); //force buffer to empty
    vector<Token> outputTokens;
    extractSentencesAndFlush( numS, outputTokens, lang );
    outputTokensXML( element, outputTokens, 0 );
  }

  void TokenizerClass::outputTokensDoc_init( folia::Document& doc ) const {
    doc.addStyle( "text/xsl", "folia.xsl" );
    if ( passthru ){
      doc.declare( folia::AnnotationType::TOKEN, "passthru", "annotator='ucto', annotatortype='auto', datetime='now()'" );
    }
    else {
      for ( const auto& s : settings ){
	doc.declare( folia::AnnotationType::TOKEN, s.second->set_file,
		     "annotator='ucto', annotatortype='auto', datetime='now()'");
      }
    }
    folia::Text *text = new folia::Text( folia::getArgs("id='" + docid + ".text'") );
    doc.append( text );
  }

  int TokenizerClass::outputTokensXML( folia::FoliaElement *root,
				       const vector<Token>& tv,
				       int parCount ) const {
    short quotelevel = 0;
    folia::FoliaElement *lastS = root;
    if  (tokDebug > 0) {
      LOG << "[outputTokensXML] root=<" << root->classname()
		      << ">" << endl;
      LOG << "[outputTokensXML] root-id=" << root->id() << endl;
    }
    bool root_is_sentence = false;
    bool root_is_structure_element = false;
    if ( root->isinstance( folia::Sentence_t ) ){
      root_is_sentence = true;
    }
    else if ( root->isinstance( folia::Paragraph_t ) //TODO: can't we do this smarter?
	      || root->isinstance( folia::Head_t )
	      || root->isinstance( folia::Note_t )
	      || root->isinstance( folia::ListItem_t )
	      || root->isinstance( folia::Part_t )
	      || root->isinstance( folia::Utterance_t )
	      || root->isinstance( folia::Caption_t )
	      || root->isinstance( folia::Cell_t )
	      || root->isinstance( folia::Event_t ) ){
      root_is_structure_element = true;
    }

    bool in_paragraph = false;
    for ( const auto& token : tv ) {
      if ( ( !root_is_structure_element && !root_is_sentence ) //TODO: instead of !root_is_structurel check if is_structure and accepts paragraphs?
	   &&
	   ( (token.role & NEWPARAGRAPH) || !in_paragraph ) ) {
	if ( tokDebug > 0 ) {
	  LOG << "[outputTokensXML] Creating paragraph" << endl;
	}
	if ( in_paragraph ){
	  if ( text_redundancy == "full" ){
	    if ( tokDebug > 0 ) {
	      LOG << "[outputTokensXML] Creating text on root: " << root->id() << endl;
	    }
	    appendText( root, outputclass );
	  }
	  else if ( text_redundancy == "none" ){
	    if ( tokDebug > 0 ) {
	      LOG << "[outputTokensXML] Removing text from root: " << root->id() << endl;
	    }
	    removeText( root, outputclass );
	  }
	  root = root->parent();
	}
	folia::KWargs args;
	args["id"] = root->doc()->id() + ".p." +  toString(++parCount);
	folia::FoliaElement *p = new folia::Paragraph( args, root->doc() );
	//	LOG << "created " << p << endl;
	root->append( p );
	root = p;
	quotelevel = 0;
      }
      if ( token.role & ENDQUOTE) {
	if ( tokDebug > 0 ){
	  LOG << "[outputTokensXML] End of quote" << endl;
	}
	quotelevel--;
	root = root->parent();
	lastS = root;
	if ( tokDebug > 0 ){
	  LOG << "[outputTokensXML] back to " << root->classname() << endl;
	}
      }
      if ( ( token.role & LINEBREAK) ){
	if  (tokDebug > 0) {
	  LOG << "[outputTokensXML] LINEBREAK!" << endl;
	}
	folia::FoliaElement *lb = new folia::Linebreak();
	root->append( lb );
	if  (tokDebug > 0){
	  LOG << "[outputTokensXML] back to " << root->classname() << endl;
	}
      }
      if ( ( token.role & BEGINOFSENTENCE)
	   && !root_is_sentence
	   && !root->isinstance( folia::Utterance_t ) ) {
	folia::KWargs args;
	string id = root->id();
	if ( id.empty() ){
	  id = root->parent()->id();
	}
	if ( !id.empty() ){
	  args["generate_id"] = id;
	}
	if ( tokDebug > 0 ) {
	  LOG << "[outputTokensXML] Creating sentence in '"
			  << args["generate_id"] << "'" << endl;
	}
	folia::FoliaElement *s = new folia::Sentence( args, root->doc() );
	root->append( s );
	string tok_lan = token.lc;
	auto it = settings.find(tok_lan);
	if ( it == settings.end() ){
	  tok_lan = root->doc()->language();
	}
	if ( !tok_lan.empty() &&
	     tok_lan != default_language
	     && tok_lan != "default" ){
	  if  (tokDebug > 0) {
	    LOG << "[outputTokensXML] set language: " << tok_lan << endl;
	  }
	  s->doc()->declare( folia::AnnotationType::LANG,
			     ISO_SET, "annotator='ucto'" );
	  set_language( s, tok_lan );
	}
	root = s;
	lastS = root;
      }
      if ( !(token.role & LINEBREAK) ){
	if  (tokDebug > 0) {
	  LOG << "[outputTokensXML] Creating word element for " << token.us << endl;
	}
	folia::KWargs args;
	string id = lastS->id();
	if ( id.empty() ){
	  id = lastS->parent()->id();
	}
	if ( !id.empty() ){
	  args["generate_id"] = id;
	}
	args["class"] = TiCC::UnicodeToUTF8( token.type );
	if ( passthru ){
	  args["set"] = "passthru";
	}
	else {
	  auto it = settings.find(token.lc);
	  if ( it == settings.end() ){
	    it = settings.find("default");
	  }
	  args["set"] = it->second->set_file;
	}
	if ( token.role & NOSPACE) {
	  args["space"]= "no";
	}
	if ( outputclass != inputclass ){
	  args["textclass"] = outputclass;
	}
	folia::FoliaElement *w = new folia::Word( args, root->doc() );
	root->append( w );
	icu::UnicodeString out = token.us;
	if (lowercase) {
	  out.toLower();
	}
	else if (uppercase) {
	  out.toUpper();
	}
	w->settext( TiCC::UnicodeToUTF8( out ), outputclass );
	if ( tokDebug > 1 ) {
	  LOG << "created " << w << " text= " <<  token.us  << "(" << outputclass << ")" << endl;
	}
      }
      if ( token.role & BEGINQUOTE) {
	if  (tokDebug > 0) {
	  LOG << "[outputTokensXML] Creating quote element" << endl;
	}
	folia::KWargs args;
	string id = root->id();
	if ( id.empty() ){
	  id = root->parent()->id();
	}
	if ( !id.empty() ){
	  args["generate_id"] = id;
	}
	folia::FoliaElement *q = new folia::Quote( args, root->doc() );
	//	LOG << "created " << q << endl;
	root->append( q );
	root = q;
	quotelevel++;
      }
      if ( ( token.role & ENDOFSENTENCE ) && (!root_is_sentence) && (!root->isinstance(folia::Utterance_t))) {
	if  (tokDebug > 0) {
	  LOG << "[outputTokensXML] End of sentence" << endl;
	}
	if ( text_redundancy == "full" ){
	  appendText( root, outputclass );
	}
	else if ( text_redundancy == "none" ){
	  removeText( root, outputclass );
	}
	if ( token.role & LINEBREAK ){
	  folia::FoliaElement *lb = new folia::Linebreak();
	  root->append( lb );
	}
	root = root->parent();
	lastS = root;
	if  (tokDebug > 0){
	  LOG << "[outputTokensXML] back to " << root->classname() << endl;
	}
      }
      in_paragraph = true;
    }
    if ( tv.size() > 0 ){
      if ( text_redundancy == "full" ){
	if ( tokDebug > 0 ) {
	  LOG << "[outputTokensXML] Creating text on root: " << root->id() << endl;
	}
	appendText( root, outputclass );
      }
      else if ( text_redundancy == "none" ){
	if ( tokDebug > 0 ) {
	  LOG << "[outputTokensXML] Removing text from root: " << root->id() << endl;
	}
	removeText( root, outputclass );
      }
    }
    if ( tokDebug > 0 ) {
      LOG << "[outputTokensXML] Done. parCount= " << parCount << endl;
    }
    return parCount;
  }

  void TokenizerClass::outputTokens( ostream& OUT,
				     const vector<Token>& tokens,
				     const bool continued ) const {
    // continued should be set to true when outputTokens is invoked multiple
    // times and it is not the first invokation
    // this makes paragraph boundaries work over multiple calls
    short quotelevel = 0;
    bool first = true;
    for ( const auto token : tokens ) {
      if ( detectPar
	   && (token.role & NEWPARAGRAPH)
	   && !verbose
	   && ( !first || continued ) ) {
	//output paragraph separator
	if (sentenceperlineoutput) {
	  OUT << endl;
	}
	else {
	  OUT << endl << endl;
	}
      }
      icu::UnicodeString s = token.us;
      if (lowercase) {
	s = s.toLower();
      }
      else if (uppercase) {
	s = s.toUpper();
      }
      OUT << s;
      if ( token.role & NEWPARAGRAPH) {
	quotelevel = 0;
      }
      if ( token.role & BEGINQUOTE) {
	++quotelevel;
      }
      if (verbose) {
	OUT << "\t" << token.type << "\t" << token.role << endl;
      }
      if ( token.role & ENDQUOTE) {
	--quotelevel;
      }

      if ( token.role & ENDOFSENTENCE) {
	if ( verbose ) {
	  if ( !(token.role & NOSPACE ) ){
	    OUT << endl;
	  }
	}
	else if ( quotelevel == 0 ) {
	  if (sentenceperlineoutput) {
	    OUT << endl;
	  }
	  else {
	    OUT << " " + eosmark;
	  }
	}
      }
      if ( ( &token != &(*tokens.rbegin()) )
	   && !verbose ) {
	if ( !( (token.role & ENDOFSENTENCE)
		&& sentenceperlineoutput ) ) {
	  OUT << " ";
	  //FBK: ADD SPACE WITHIN QUOTE CONTEXT IN ANY CASE
	}
	else if ( (quotelevel > 0)
		  && sentenceperlineoutput ) {
	  OUT << " ";
	}
      }
    }
  }

  int TokenizerClass::countSentences( bool forceentirebuffer ) {
    //Return the number of *completed* sentences in the token buffer

    //Performs  extra sanity checks at the same time! Making sure
    //BEGINOFSENTENCE and ENDOFSENTENCE always pair up, and that TEMPENDOFSENTENCE roles
    //are converted to proper ENDOFSENTENCE markers

    short quotelevel = 0;
    int count = 0;
    const int size = tokens.size();
    int begin = 0;
    int i = 0;
    for ( auto& token : tokens ) {
      if (tokDebug >= 5){
	LOG << "[countSentences] buffer#" <<i
			<< " word=[" << token.us
			<< "] role=" << token.role
			<< ", quotelevel="<< quotelevel << endl;
      }
      if (token.role & NEWPARAGRAPH) quotelevel = 0;
      if (token.role & BEGINQUOTE) quotelevel++;
      if (token.role & ENDQUOTE) quotelevel--;
      if ( forceentirebuffer
	   && (token.role & TEMPENDOFSENTENCE)
	   && (quotelevel == 0)) {
	//we thought we were in a quote, but we're not... No end quote was found and an end is forced now.
	//Change TEMPENDOFSENTENCE to ENDOFSENTENCE and make sure sentences match up sanely
	token.role ^= TEMPENDOFSENTENCE;
	token.role |= ENDOFSENTENCE;
	tokens[begin].role |= BEGINOFSENTENCE;
      }
      if ( (token.role & ENDOFSENTENCE)
	   && (quotelevel == 0) ) {
	begin = i + 1;
	count++;
	if (tokDebug >= 5){
	  LOG << "[countSentences] SENTENCE #" << count << " found" << endl;
	}
	if ( begin < size ){
	  tokens[begin].role |= BEGINOFSENTENCE;
	}
      }
      if ( forceentirebuffer
	   && ( i == size - 1)
	   && !(token.role & ENDOFSENTENCE) )  {
	//last token of buffer
	count++;
	token.role |= ENDOFSENTENCE;
	if (tokDebug >= 5){
	  LOG << "[countSentences] SENTENCE #" << count << " *FORCIBLY* ended" << endl;
	}
      }
      ++i;
    }
    return count;
  }

  int TokenizerClass::flushSentences( int sentences,
				      const string& lang ) {
    //Flush n sentences from the buffer, returns the number of tokens left
    short quotelevel = 0;
    const int size = tokens.size();
    if (sentences == 0) return size;
    int begin = 0;
    for (int i = 0; (i < size ) && (sentences > 0); i++) {
      if (tokens[i].role & NEWPARAGRAPH) quotelevel = 0;
      if (tokens[i].role & BEGINQUOTE) quotelevel++;
      if (tokens[i].role & ENDQUOTE) quotelevel--;
      if ((tokens[i].role & ENDOFSENTENCE) && (quotelevel == 0)) {
	begin = i + 1;
	--sentences;
      }
    }
    if (begin == 0) {
      throw uLogicError("Unable to flush, not so many sentences in buffer");
    }
    if (begin == size) {
      tokens.clear();
      if ( !passthru ){
	settings[lang]->quotes.clearStack();
      }
    }
    else {
      tokens.erase (tokens.begin(),tokens.begin()+begin);
      if ( !passthru ){
	if ( !settings[lang]->quotes.emptyStack() ) {
	  settings[lang]->quotes.flushStack( begin );
	}
      }
    }
    //After flushing, the first token still in buffer (if any) is always a BEGINOFSENTENCE:
    if (!tokens.empty()) {
      tokens[0].role |= BEGINOFSENTENCE;
    }
    return tokens.size();
  }

  vector<Token> TokenizerClass::getSentence( int index ) {
    vector<Token> outToks;
    int count = 0;
    const int size = tokens.size();
    short quotelevel = 0;
    size_t begin = 0;
    size_t end = 0;
    for ( int i = 0; i < size; ++i ) {
      if (tokens[i].role & NEWPARAGRAPH) {
	quotelevel = 0;
      }
      else if (tokens[i].role & ENDQUOTE) {
	--quotelevel;
      }
      if ( (tokens[i].role & BEGINOFSENTENCE)
	   && (quotelevel == 0)) {
	begin = i;
      }
      //FBK: QUOTELEVEL GOES UP BEFORE begin IS UPDATED... RESULTS IN DUPLICATE OUTPUT
      if (tokens[i].role & BEGINQUOTE) {
	++quotelevel;
      }

      if ((tokens[i].role & ENDOFSENTENCE) && (quotelevel == 0)) {
	if (count == index) {
	  end = i;
	  tokens[begin].role |= BEGINOFSENTENCE;  //sanity check
	  if (tokDebug >= 1){
	    LOG << "[tokenize] extracted sentence " << index << ", begin="<<begin << ",end="<< end << endl;
	  }
	  for ( size_t i=begin; i <= end; ++i ){
	    outToks.push_back( tokens[i] );
	  }
	  return outToks;
	}
	++count;
      }
    }
    throw uRangeError( "No sentence exists with the specified index: "
		       + toString( index ) );
    return outToks;
  }

  string TokenizerClass::getSentenceString( unsigned int i ){
    vector<Token> v = getSentence( i );
    if ( !v.empty() ){
      //This only makes sense in non-verbose mode, force verbose=false
      stringstream TMPOUT;
      const bool tv = verbose;
      verbose = false;
      outputTokens( TMPOUT, v );
      verbose = tv;
      return TMPOUT.str();
    }
    return "";
  }

  vector<string> TokenizerClass::getSentences() {
    vector<string> sentences;
    int numS = countSentences(true); //force buffer to empty
    for (int i = 0; i < numS; i++) {
      string tmp = getSentenceString( i );
      sentences.push_back( tmp );
    }
    return sentences;
  }

  // FBK: return true if character is a quote.
  bool TokenizerClass::u_isquote( UChar32 c, const Quoting& quotes ) const {
    bool quote = false;
    if ( u_hasBinaryProperty( c, UCHAR_QUOTATION_MARK )
	 || c == '`'
	 || c == U'´' ) {
      // M$ users use the spacing grave and acute accents often as a
      // quote (apostroph) but is DOESN`T have the UCHAR_QUOTATION_MARK property
      // so trick that
      quote = true;
    }
    else {
      icu::UnicodeString opening = quotes.lookupOpen( c );
      if (!opening.isEmpty()) {
	quote = true;
      }
      else {
	icu::UnicodeString closing = quotes.lookupClose( c );
	if (!closing.isEmpty()) {
	  quote = true;
	}
      }
    }
    return quote;
  }

  //FBK: USED TO CHECK IF CHARACTER AFTER QUOTE IS AN BOS.
  //MOSTLY THE SAME AS ABOVE, EXCEPT WITHOUT CHECK FOR PUNCTUATION
  //BECAUSE: '"Hoera!", zei de man' MUST NOT BE SPLIT ON ','..
  bool is_BOS( UChar32 c ){
    bool is_bos = false;
    UBlockCode s = ublock_getCode(c);
    //test for languages that distinguish case
    if ( (s == UBLOCK_BASIC_LATIN) || (s == UBLOCK_GREEK)
	 || (s == UBLOCK_CYRILLIC) || (s == UBLOCK_GEORGIAN)
	 || (s == UBLOCK_ARMENIAN) || (s == UBLOCK_DESERET)) {
      if ( u_isupper(c) || u_istitle(c) ) {
	//next 'word' starts with more punctuation or with uppercase
	is_bos = true;
      }
    }
    return is_bos;
  }

  bool TokenizerClass::resolveQuote( int endindex,
				     const icu::UnicodeString& open,
				     Quoting& quotes ) {
    //resolve a quote
    int stackindex = -1;
    int beginindex = quotes.lookup( open, stackindex );

    if (beginindex >= 0) {
      if (tokDebug >= 2) {
	LOG << "[resolveQuote] Quote found, begin="<< beginindex << ", end="<< endindex << endl;
      }

      if (beginindex > endindex) {
	throw uRangeError( "Begin index for quote is higher than end index!" );
      }

      //We have a quote!

      //resolve sentences within quote, all sentences must be full sentences:
      int beginsentence = beginindex + 1;
      int expectingend = 0;
      int subquote = 0;
      int size = tokens.size();
      for (int i = beginsentence; i < endindex; i++) {
	if (tokens[i].role & BEGINQUOTE) subquote++;

	if (subquote == 0) {
	  if (tokens[i].role & BEGINOFSENTENCE) expectingend++;
	  if (tokens[i].role & ENDOFSENTENCE) expectingend--;

	  if (tokens[i].role & TEMPENDOFSENTENCE) {
	    tokens[i].role ^= TEMPENDOFSENTENCE;
	    tokens[i].role |= ENDOFSENTENCE;
	    tokens[beginsentence].role |= BEGINOFSENTENCE;
	    beginsentence = i + 1;
	  }
	  // In case of nested quoted sentences, such as:
	  //    MvD: "Nou, Van het Gouden Been ofzo herinner ik mij als kind: 'Waar is mijn gouden been?'"
	  // the BEGINOFSENTENCE is only set for the inner quoted sentence 'Waar is mijn gouden been'. However,
	  // We also need one for the outser sentence.
	}
	else if ( (tokens[i].role & ENDQUOTE)
		  && (tokens[i].role & ENDOFSENTENCE)) {
	  tokens[beginsentence].role |= BEGINOFSENTENCE;
	  beginsentence = i + 1;
	}
	if (tokens[i].role & ENDQUOTE) subquote--;
      }
      if ((expectingend == 0) && (subquote == 0)) {
	//ok, all good, mark the quote:
	tokens[beginindex].role |= BEGINQUOTE;
	tokens[endindex].role |= ENDQUOTE;
      }
      else if ((expectingend == 1) && (subquote == 0) && !(tokens[endindex - 1].role & ENDOFSENTENCE)) {
	//missing one endofsentence, we can correct, last token in quote token is endofsentence:
	if ( tokDebug >= 2 ) {
	  LOG << "[resolveQuote] Missing endofsentence in quote, fixing... " << expectingend << endl;
	}
	tokens[endindex - 1].role |= ENDOFSENTENCE;
	//mark the quote
	tokens[beginindex].role |= BEGINQUOTE;
	tokens[endindex].role |= ENDQUOTE;
      }
      else {
	if ( tokDebug >= 2) {
	  LOG << "[resolveQuote] Quote can not be resolved, unbalanced sentences or subquotes within quote, skipping... (expectingend=" << expectingend << ",subquote=" << subquote << ")" << endl;
	}
	//something is wrong. Sentences within quote are not balanced, so we won't mark the quote.
      }
      //remove from stack (ok, granted, stack is a bit of a misnomer here)
      quotes.eraseAtPos( stackindex );
      //FBK: ENDQUOTES NEED TO BE MARKED AS ENDOFSENTENCE IF THE PREVIOUS TOKEN
      //WAS AN ENDOFSENTENCE. OTHERWISE THE SENTENCES WILL NOT BE SPLIT.
      if ((tokens[endindex].role & ENDQUOTE) && (tokens[endindex-1].role & ENDOFSENTENCE)) {
        //FBK: CHECK FOR EOS AFTER QUOTES
        if ((endindex+1 == size) || //FBK: endindex EQUALS TOKEN SIZE, MUST BE EOSMARKERS
            ((endindex + 1 < size) && (is_BOS(tokens[endindex+1].us[0])))) {
	  tokens[endindex].role |= ENDOFSENTENCE;
	  // FBK: CHECK IF NEXT TOKEN IS A QUOTE AND NEXT TO THE QUOTE A BOS
        }
	else if ( endindex + 2 < size
		  && u_isquote( tokens[endindex+1].us[0], quotes )
		  && is_BOS( tokens[endindex+2].us[0] ) ) {
	  tokens[endindex].role |= ENDOFSENTENCE;
	  // If the current token is an ENDQUOTE and the next token is a quote and also the last token,
	  // the current token is an EOS.
        }
	else if ( endindex + 2 == size
		  && u_isquote( tokens[endindex+1].us[0], quotes ) ) {
	  tokens[endindex].role |= ENDOFSENTENCE;
        }
      }
      return true;
    }
    else {
      return false;
    }
  }

  bool TokenizerClass::detectEos( size_t i,
				  const icu::UnicodeString& eosmarkers,
				  const Quoting& quotes ) const {
    bool is_eos = false;
    UChar32 c = tokens[i].us.char32At(0);
    if ( c == '.' || eosmarkers.indexOf( c ) >= 0 ){
      if (i + 1 == tokens.size() ) {	//No next character?
	is_eos = true; //Newline after eosmarker
      }
      else {
	UChar32 c = tokens[i+1].us.char32At(0);
	if ( u_isquote( c, quotes ) ){
	  // next word is quote
	  if ( detectQuotes )
	    is_eos = true;
	  else if ( i + 2 < tokens.size() ) {
	    UChar32 c = tokens[i+2].us.char32At(0);
	    if ( u_isupper(c) || u_istitle(c) || u_ispunct(c) ){
	      //next 'word' after quote starts with uppercase or is punct
	      is_eos = true;
	    }
	  }
	}
	else if ( tokens[i].us.length() > 1 ){
	  // PUNCTUATION multi...
	  if ( u_isupper(c) || u_istitle(c) )
	    is_eos = true;
	}
	else
	  is_eos = true;
      }
    }
    return is_eos;
  }

  void TokenizerClass::detectQuoteBounds( const int i,
					  Quoting& quotes ) {
    UChar32 c = tokens[i].us.char32At(0);
    //Detect Quotation marks
    if ((c == '"') || ( icu::UnicodeString(c) == "＂") ) {
      if (tokDebug > 1 ){
	LOG << "[detectQuoteBounds] Standard double-quote (ambiguous) found @i="<< i << endl;
      }
      if (!resolveQuote(i,c,quotes)) {
	if (tokDebug > 1 ) {
	  LOG << "[detectQuoteBounds] Doesn't resolve, so assuming beginquote, pushing to stack for resolution later" << endl;
	}
	quotes.push( i, c );
      }
    }
    else if ( c == '\'' ) {
      if (tokDebug > 1 ){
	LOG << "[detectQuoteBounds] Standard single-quote (ambiguous) found @i="<< i << endl;
      }
      if (!resolveQuote(i,c,quotes)) {
	if (tokDebug > 1 ) {
	  LOG << "[detectQuoteBounds] Doesn't resolve, so assuming beginquote, pushing to stack for resolution later" << endl;
	}
	quotes.push( i, c );
      }
    }
    else {
      icu::UnicodeString close = quotes.lookupOpen( c );
      if ( !close.isEmpty() ){ // we have a opening quote
	if ( tokDebug > 1 ) {
	  LOG << "[detectQuoteBounds] Opening quote found @i="<< i << ", pushing to stack for resolution later..." << endl;
	}
	quotes.push( i, c ); // remember it
      }
      else {
	icu::UnicodeString open = quotes.lookupClose( c );
	if ( !open.isEmpty() ) { // we have a closeing quote
	  if (tokDebug > 1 ) {
	    LOG << "[detectQuoteBounds] Closing quote found @i="<< i << ", attempting to resolve..." << endl;
	  }
	  if ( !resolveQuote( i, open, quotes )) {
	    // resolve the matching opening
	    if (tokDebug > 1 ) {
	      LOG << "[detectQuoteBounds] Unable to resolve" << endl;
	    }
	  }
	}
      }
    }
  }

  bool isClosing( const Token& tok ){
    if ( tok.us.length() == 1 &&
	 ( tok.us[0] == ')' || tok.us[0] == '}'
	   || tok.us[0] == ']' || tok.us[0] == '>' ) )
      return true;
    return false;
  }

  void TokenizerClass::detectSentenceBounds( const int offset,
					     const string& lang ){
    //find sentences
    const int size = tokens.size();
    for (int i = offset; i < size; i++) {
      if (tokDebug > 1 ){
	LOG << "[detectSentenceBounds] i="<< i << " word=["
			<< tokens[i].us
			<< "] type=" << tokens[i].type
			<< ", role=" << tokens[i].role << endl;
      }
      if ( tokens[i].type.startsWith("PUNCTUATION") ){
	if ((tokDebug > 1 )){
	  LOG << "[detectSentenceBounds] PUNCTUATION FOUND @i="
			  << i << endl;
	}
	// we have some kind of punctuation. Does it mark an eos?
	bool is_eos = detectEos( i,
				 settings[lang]->eosmarkers,
				 settings[lang]->quotes );
	if (is_eos) {
	  if ((tokDebug > 1 )){
	    LOG << "[detectSentenceBounds] EOS FOUND @i="
			    << i << endl;
	  }
	  tokens[i].role |= ENDOFSENTENCE;
	  //if this is the end of the sentence, the next token is the beginning of a new one
	  if ((i + 1 < size) && !(tokens[i+1].role & BEGINOFSENTENCE))
	    tokens[i+1].role |= BEGINOFSENTENCE;
	  //if previous token is EOS and not BOS, it will stop being EOS, as this one will take its place
	  if ((i > 0) && (tokens[i-1].role & ENDOFSENTENCE) && !(tokens[i-1].role & BEGINOFSENTENCE) ) {
	    tokens[i-1].role ^= ENDOFSENTENCE;
	    if (tokens[i].role & BEGINOFSENTENCE) {
	      tokens[i].role ^= BEGINOFSENTENCE;
	    }
	  }
	}
	else if ( isClosing(tokens[i] ) ) {
	  // we have a closing symbol
	  if ( tokDebug > 1 ){
	    LOG << "[detectSentenceBounds] Close FOUND @i=" << i << endl;
	  }
	  //if previous token is EOS and not BOS, it will stop being EOS, as this one will take its place
	  if ((i > 0) && (tokens[i-1].role & ENDOFSENTENCE) && !(tokens[i-1].role & BEGINOFSENTENCE) ) {
	    tokens[i-1].role ^= ENDOFSENTENCE;
	    if (tokens[i].role & BEGINOFSENTENCE) {
	      tokens[i].role ^= BEGINOFSENTENCE;
	    }
	  }
	}
      }
    }
    for (int i = size-1; i > offset; --i ) {
      // at the end of the buffer there may be some PUNCTUATION which
      // has spurious ENDOFSENTENCE and BEGINOFSENTENCE annotation
      // fix this up to avoid sentences containing only punctuation
      if (tokDebug > 1 ){
	LOG << "[detectSentenceBounds:fixup] i="<< i << " word=["
			<< tokens[i].us
			<< "] type=" << tokens[i].type
			<< ", role=" << tokens[i].role << endl;
      }
      if ( tokens[i].type.startsWith("PUNCTUATION") ) {
	if (tokens[i].role & BEGINOFSENTENCE) {
	  tokens[i].role ^= BEGINOFSENTENCE;
	}
	if ( i != size-1 ){
	  if (tokens[i].role & ENDOFSENTENCE) {
	    tokens[i].role ^= ENDOFSENTENCE;
	  }
	}
      }
      else
	break;
    }
  }

  void TokenizerClass::detectQuotedSentenceBounds( const int offset,
						   const string& lang ){
    //find sentences
    const int size = tokens.size();
    for (int i = offset; i < size; i++) {
      if (tokDebug > 1 ){
	LOG << "[detectQuotedSentenceBounds] i="<< i << " word=["
			<< tokens[i].us
			<<"] role=" << tokens[i].role << endl;
      }
      if ( tokens[i].type.startsWith("PUNCTUATION") ){
	// we have some kind of punctuation. Does it mark an eos?
	bool is_eos = detectEos( i,
				 settings[lang]->eosmarkers,
				 settings[lang]->quotes );
	if (is_eos) {
	  if ( !settings[lang]->quotes.emptyStack() ) {
	    if ( tokDebug > 1 ){
	      LOG << "[detectQuotedSentenceBounds] Preliminary EOS FOUND @i=" << i << endl;
	    }
	    //if there are quotes on the stack, we set a temporary EOS marker, to be resolved later when full quote is found.
	    tokens[i].role |= TEMPENDOFSENTENCE;
	    //If previous token is also TEMPENDOFSENTENCE, it stops being so in favour of this one
	    if ((i > 0) && (tokens[i-1].role & TEMPENDOFSENTENCE))
	      tokens[i-1].role ^= TEMPENDOFSENTENCE;
	  }
	  else if (!sentenceperlineinput)  { //No quotes on stack (and no one-sentence-per-line input)
	    if ( tokDebug > 1 ){
	      LOG << "[detectQuotedSentenceBounds] EOS FOUND @i=" << i << endl;
	    }
	    tokens[i].role |= ENDOFSENTENCE;
	    //if this is the end of the sentence, the next token is the beginning of a new one
	    if ((i + 1 < size) && !(tokens[i+1].role & BEGINOFSENTENCE))
	      tokens[i+1].role |= BEGINOFSENTENCE;
	    //if previous token is EOS and not BOS, it will stop being EOS, as this one will take its place
	    if ((i > 0) && (tokens[i-1].role & ENDOFSENTENCE) && !(tokens[i-1].role & BEGINOFSENTENCE) ) {
	      tokens[i-1].role ^= ENDOFSENTENCE;
	      if (tokens[i].role & BEGINOFSENTENCE) {
		tokens[i].role ^= BEGINOFSENTENCE;
	      }
	    }
	  }
	}
	else if ( isClosing(tokens[i] ) ) {
	  // we have a closing symbol
	  if ( tokDebug > 1 ){
	    LOG << "[detectSentenceBounds] Close FOUND @i=" << i << endl;
	  }
	  //if previous token is EOS and not BOS, it will stop being EOS, as this one will take its place
	  if ((i > 0) && (tokens[i-1].role & ENDOFSENTENCE) && !(tokens[i-1].role & BEGINOFSENTENCE) ) {
	    tokens[i-1].role ^= ENDOFSENTENCE;
	    if (tokens[i].role & BEGINOFSENTENCE) {
	      tokens[i].role ^= BEGINOFSENTENCE;
	    }
	  }
	}
	//check quotes
	detectQuoteBounds( i, settings[lang]->quotes );
      }
    }
  }

  void TokenizerClass::passthruLine( const string& s, bool& bos ) {
    // string wrapper
    icu::UnicodeString us = convert( s, inputEncoding );;
    passthruLine( us, bos );
  }

  void TokenizerClass::passthruLine( const icu::UnicodeString& input, bool& bos ) {
    if (tokDebug) {
      LOG << "[passthruLine] input: line=[" << input << "]" << endl;
    }
    bool alpha = false, num = false, punct = false;
    icu::UnicodeString word;
    StringCharacterIterator sit(input);
    while ( sit.hasNext() ){
      UChar32 c = sit.current32();
      if ( u_isspace(c)) {
	if ( word.isEmpty() ){
	  // a leading space. Don't waste time on it. SKIP
	  sit.next32();
	  continue;
	}
	// so a trailing space. handle the found word.
	if (tokDebug){
	  LOG << "[passthruLine] word=[" << word << "]" << endl;
	}
	if ( word == eosmark ) {
	  word = "";
	  if (!tokens.empty())
	    tokens.back().role |= ENDOFSENTENCE;
	  bos = true;
	}
	else {
	  icu::UnicodeString type;
	  if (alpha && !num && !punct) {
	    type = type_word;
	  }
	  else if (num && !alpha && !punct) {
	    type = type_number;
	  }
	  else if (punct && !alpha && !num) {
	    type = type_punctuation;
	  }
	  else {
	    type = type_unknown;
	  }
	  if ( doPunctFilter
	       && ( type == type_punctuation || type == type_currency ||
		    type == type_emoticon || type == type_picto ) ) {
	    if (tokDebug >= 2 ){
	      LOG << "   [passThruLine] skipped PUNCTUATION ["
			      << input << "]" << endl;
	    }
	    if ( !tokens.empty() && tokens.back().role & NOSPACE ){
	      tokens.back().role ^= NOSPACE;
	    }
	  }
	  else {
	    if ( norm_set.find( type ) != norm_set.end() ){
	      word = "{{" + type + "}}";
	    }
	    if (bos) {
	      tokens.push_back( Token( type, word , BEGINOFSENTENCE ) );
	      bos = false;
	    }
	    else {
	      tokens.push_back( Token( type, word ) );
	    }
	  }
	  alpha = false;
	  num = false;
	  punct = false;
          word = "";
	}
      }
      else {
	if ( u_isalpha(c)) {
	  alpha = true;
	}
	else if (u_ispunct(c)) {
	  punct = true;
	}
	else if (u_isdigit(c)) {
	  num = true;
	}
	word += c;
      }
      sit.next32();
    }
    if (word != "") {
      if ( word == eosmark ) {
	word = "";
	if (!tokens.empty())
	  tokens.back().role |= ENDOFSENTENCE;
      }
      else {
	icu::UnicodeString type;
	if (alpha && !num && !punct) {
	  type = type_word;
	}
	else if (num && !alpha && !punct) {
	  type = type_number;
	}
	else if (punct && !alpha && !num) {
	  type = type_punctuation;
	}
	else {
	  type = type_unknown;
	}
	if ( doPunctFilter
	     && ( type == type_punctuation || type == type_currency ||
		  type == type_emoticon || type == type_picto ) ) {
	  if (tokDebug >= 2 ){
	    LOG << "   [passThruLine] skipped PUNCTUATION ["
			    << input << "]" << endl;
	  }
	  if ( !tokens.empty() && tokens.back().role & NOSPACE ){
	    tokens.back().role ^= NOSPACE;
	  }
	}
	else {
	  if ( norm_set.find( type ) != norm_set.end() ){
	    word = "{{" + type + "}}";
	  }
	  if (bos) {
	    tokens.push_back( Token( type, word , BEGINOFSENTENCE ) );
	    bos = false;
	  }
	  else {
	    tokens.push_back( Token( type, word ) );
	  }
	}
      }
    }
    if ( sentenceperlineinput && tokens.size() > 0 ) {
      tokens[0].role |= BEGINOFSENTENCE;
      tokens.back().role |= ENDOFSENTENCE;
    }
  }

  string TokenizerClass::checkBOM( istream& in ){
    string result = inputEncoding;
    if ( &in == &cin ){
      return result;
    }
    streampos pos = in.tellg();
    string s;
    in >> s;
    UErrorCode err = U_ZERO_ERROR;
    int32_t bomLength = 0;
    const char *encoding = ucnv_detectUnicodeSignature( s.c_str(), s.length(),
							&bomLength, &err);
    if ( bomLength ){
      if ( tokDebug ){
	LOG << "Autodetected encoding: " << encoding << endl;
      }
      result = encoding;
      if ( result == "UTF16BE"
	   || result == "UTF-16BE" ){
	// throw uCodingError( string(" BigEndian UTF16 is not supported.\n")
	// 		    + "Please use 'iconv -f UTF16BE -t UTF16LE'"
	// 		    + " to convert your input to a supported format" );
	result = "UTF16BE";
      }
    }
    in.seekg( pos + (streampos)bomLength );
    return result;
  }

  // string wrapper
  int TokenizerClass::tokenizeLine( const string& s,
				    const string& lang ){
    icu::UnicodeString uinputstring = convert( s, inputEncoding );
    return tokenizeLine( uinputstring, lang, "" );
  }

  // icu::UnicodeString wrapper
  int TokenizerClass::tokenizeLine( const icu::UnicodeString& u,
				    const string& lang ){
    return tokenizeLine( u, lang, "" );
  }

  bool u_isemo( UChar32 c ){
    UBlockCode s = ublock_getCode(c);
    return s == UBLOCK_EMOTICONS;
  }

  bool u_ispicto( UChar32 c ){
    UBlockCode s = ublock_getCode(c);
    return s == UBLOCK_MISCELLANEOUS_SYMBOLS_AND_PICTOGRAPHS ;
  }

  bool u_iscurrency( UChar32 c ){
    return u_charType( c ) == U_CURRENCY_SYMBOL;
  }

  bool u_issymbol( UChar32 c ){
    return u_charType( c ) == U_CURRENCY_SYMBOL
      || u_charType( c ) == U_MATH_SYMBOL
      || u_charType( c ) == U_MODIFIER_SYMBOL
      || u_charType( c ) == U_OTHER_SYMBOL;
  }

  const icu::UnicodeString& detect_type( UChar32 c ){
    if ( u_isspace(c)) {
      return type_space;
    }
    else if ( u_iscurrency(c)) {
      return type_currency;
    }
    else if ( u_ispunct(c)) {
      return type_punctuation;
    }
    else if ( u_isemo( c ) ) {
      return type_emoticon;
    }
    else if ( u_ispicto( c ) ) {
      return type_picto;
    }
    else if ( u_isalpha(c)) {
      return type_word;
    }
    else if ( u_isdigit(c)) {
      return type_number;
    }
    else if ( u_issymbol(c)) {
      return type_symbol;
    }
    else {
      return type_unknown;
    }
  }

  std::string toString( int8_t c ){
    switch ( c ){
    case 0:
      return "U_UNASSIGNED";
    case 1:
      return "U_UPPERCASE_LETTER";
    case 2:
      return "U_LOWERCASE_LETTER";
    case 3:
      return "U_TITLECASE_LETTER";
    case 4:
      return "U_MODIFIER_LETTER";
    case 5:
      return "U_OTHER_LETTER";
    case 6:
      return "U_NON_SPACING_MARK";
    case 7:
      return "U_ENCLOSING_MARK";
    case 8:
      return "U_COMBINING_SPACING_MARK";
    case 9:
      return "U_DECIMAL_DIGIT_NUMBER";
    case 10:
      return "U_LETTER_NUMBER";
    case 11:
      return "U_OTHER_NUMBER";
    case 12:
      return "U_SPACE_SEPARATOR";
    case 13:
      return "U_LINE_SEPARATOR";
    case 14:
      return "U_PARAGRAPH_SEPARATOR";
    case 15:
      return "U_CONTROL_CHAR";
    case 16:
      return "U_FORMAT_CHAR";
    case 17:
      return "U_PRIVATE_USE_CHAR";
    case 18:
      return "U_SURROGATE";
    case 19:
      return "U_DASH_PUNCTUATION";
    case 20:
      return "U_START_PUNCTUATION";
    case 21:
      return "U_END_PUNCTUATION";
    case 22:
      return "U_CONNECTOR_PUNCTUATION";
    case 23:
      return "U_OTHER_PUNCTUATION";
    case 24:
      return "U_MATH_SYMBOL";
    case 25:
      return "U_CURRENCY_SYMBOL";
    case 26:
      return "U_MODIFIER_SYMBOL";
    case 27:
      return "U_OTHER_SYMBOL";
    case 28:
      return "U_INITIAL_PUNCTUATION";
    case 29:
      return "U_FINAL_PUNCTUATION";
    default:
      return "OMG NO CLUE WHAT KIND OF SYMBOL THIS IS: "
	+ TiCC::toString( int(c) );
    }
  }

  int TokenizerClass::tokenizeLine( const icu::UnicodeString& originput,
				    const string& _lang,
				    const string& id ){
    string lang = _lang;
    if ( lang.empty() ){
      lang = "default";
    }
    else {
      auto const it = settings.find( lang );
      if ( it == settings.end() ){
	LOG << "tokenizeLine: no settings found for language=" + lang << endl
	    << "using the default language instead:" << default_language << endl;
	lang = "default";
      }
    }
    if (tokDebug){
      LOG << "[tokenizeLine] input: line=["
	  << originput << "] (" << lang << ")" << endl;
    }
    icu::UnicodeString input = normalizer.normalize( originput );
    if ( doFilter ){
      input = settings[lang]->filter.filter( input );
    }
    if ( input.isBogus() ){ //only tokenize valid input
      if ( id.empty() ){
	LOG << "ERROR: Invalid UTF-8 in line:" << linenum << endl
	    << "   '" << input << "'" << endl;
      }
      else {
	LOG << "ERROR: Invalid UTF-8 in element:" << id << endl
	    << "   '" << input << "'" << endl;
      }
      return 0;
    }
    int32_t len = input.countChar32();
    if (tokDebug){
      LOG << "[tokenizeLine] filtered input: line=["
		      << input << "] (" << len
		      << " unicode characters)" << endl;
    }
    const int begintokencount = tokens.size();
    if (tokDebug) {
      LOG << "[tokenizeLine] Tokens still in buffer: " << begintokencount << endl;
    }

    bool tokenizeword = false;
    bool reset = false;
    //iterate over all characters
    icu::UnicodeString word;
    StringCharacterIterator sit(input);
    long int i = 0;
    long int tok_size = 0;
    while ( sit.hasNext() ){
      UChar32 c = sit.current32();
      if ( tokDebug > 8 ){
	icu::UnicodeString s = c;
	int8_t charT = u_charType( c );
	LOG << "examine character: " << s << " type= "
	    << toString( charT  ) << endl;
      }
      if (reset) { //reset values for new word
	reset = false;
	tok_size = 0;
	if (!u_isspace(c))
	  word = c;
	else
	  word = "";
	tokenizeword = false;
      }
      else {
	if ( !u_isspace(c) ){
	  word += c;
	}
      }
      if ( u_isspace(c) || i == len-1 ){
	if (tokDebug){
	  LOG << "[tokenizeLine] space detected, word=["
			  << word << "]" << endl;
	}
	if ( i == len-1 ) {
	  if ( u_ispunct(c)
	       || u_isdigit(c)
	       || u_isquote( c, settings[lang]->quotes )
	       || u_isemo(c) ){
	    tokenizeword = true;
	  }
	}
	int expliciteosfound = -1;
	if ( word.length() >= eosmark.length() ) {
	  expliciteosfound = word.lastIndexOf(eosmark);

	  if (expliciteosfound != -1) { // word contains eosmark
	    if ( tokDebug >= 2){
	      LOG << "[tokenizeLine] Found explicit EOS marker @"<<expliciteosfound << endl;
	    }
	    int eospos = tokens.size()-1;
	    if (expliciteosfound > 0) {
	      icu::UnicodeString realword;
	      word.extract(0,expliciteosfound,realword);
	      if (tokDebug >= 2) {
		LOG << "[tokenizeLine] Prefix before EOS: "
				<< realword << endl;
	      }
	      tokenizeWord( realword, false, lang );
	      eospos++;
	    }
	    if ( expliciteosfound + eosmark.length() < word.length() ){
	      icu::UnicodeString realword;
	      word.extract( expliciteosfound+eosmark.length(),
			    word.length() - expliciteosfound - eosmark.length(),
			    realword );
	      if (tokDebug >= 2){
		LOG << "[tokenizeLine] postfix after EOS: "
				<< realword << endl;
	      }
	      tokenizeWord( realword, true, lang );
	    }
	    if ( !tokens.empty() && eospos >= 0 ) {
	      if (tokDebug >= 2){
		LOG << "[tokenizeLine] Assigned EOS" << endl;
	      }
	      tokens[eospos].role |= ENDOFSENTENCE;
	    }
	  }
	}
	if ( word.length() > 0
	     && expliciteosfound == -1 ) {
	  if (tokDebug >= 2){
	    LOG << "[tokenizeLine] Further tokenisation necessary for: ["
			    << word << "]" << endl;
	  }
	  if ( tokenizeword ) {
	    tokenizeWord( word, true, lang );
	  }
	  else {
	    tokenizeWord( word, true, lang, type_word );
	  }
	}
	//reset values for new word
	reset = true;
      }
      else if ( u_ispunct(c)
		|| u_isdigit(c)
		|| u_isquote( c, settings[lang]->quotes )
		|| u_isemo(c) ){
	if (tokDebug){
	  LOG << "[tokenizeLine] punctuation or digit detected, word=["
			  << word << "]" << endl;
	}
	//there is punctuation or digits in this word, mark to run through tokeniser
	tokenizeword = true;
      }
      sit.next32();
      ++i;
      ++tok_size;
      if ( tok_size > 2500 ){
	if ( id.empty() ){
	  LOG << "Ridiculously long word/token (over 2500 characters) detected "
	      << "in line: " << linenum << ". Skipped ..." << endl;
	  LOG << "The line starts with " << icu::UnicodeString( word, 0, 75 )
	      << "..." << endl;
	}
	else {
	  LOG << "Ridiculously long word/token (over 2500 characters) detected "
	      << "in element: " << id << ". Skipped ..." << endl;
	  LOG << "The text starts with " << icu::UnicodeString( word, 0, 75 )
	      << "..." << endl;
	}
	return 0;
      }
    }
    int numNewTokens = tokens.size() - begintokencount;
    if ( numNewTokens > 0 ){
      if (paragraphsignal) {
	tokens[begintokencount].role |= NEWPARAGRAPH | BEGINOFSENTENCE;
	paragraphsignal = false;
      }
      if ( detectBounds ){
	//find sentence boundaries
	if (sentenceperlineinput) {
	  tokens[begintokencount].role |= BEGINOFSENTENCE;
	  tokens.back().role |= ENDOFSENTENCE;
	  if ( detectQuotes ){
	    detectQuotedSentenceBounds( begintokencount );
	  }
	}
	else {
	  if ( detectQuotes ){
	    detectQuotedSentenceBounds( begintokencount );
	  }
	  else {
	    detectSentenceBounds( begintokencount );
	  }
	}
      }
    }
    return numNewTokens;
  }

  void TokenizerClass::tokenizeWord( const icu::UnicodeString& input,
				     bool space,
				     const string& lang,
				     const icu::UnicodeString& assigned_type ) {
    bool recurse = !assigned_type.isEmpty();

    int32_t inpLen = input.countChar32();
    if ( tokDebug > 2 ){
      if ( recurse ){
	LOG << "   [tokenizeWord] Recurse Input: (" << inpLen << ") "
	    << "word=[" << input << "], type=" << assigned_type
	    << " Space=" << (space?"TRUE":"FALSE") << endl;
      }
      else {
	LOG << "   [tokenizeWord] Input: (" << inpLen << ") "
	    << "word=[" << input << "]"
	    << " Space=" << (space?"TRUE":"FALSE") << endl;      }
    }
    if ( input == eosmark ) {
      if (tokDebug >= 2){
	LOG << "   [tokenizeWord] Found explicit EOS marker" << endl;
      }
      if (!tokens.empty()) {
	if (tokDebug >= 2){
	  LOG << "   [tokenizeWord] Assigned EOS" << endl;
	}
	tokens.back().role |= ENDOFSENTENCE;
      }
      else {
	LOG << "[WARNING] Found explicit EOS marker by itself, this will have no effect!" << endl;
      }
      return;
    }

    if ( inpLen == 1) {
      //single character, no need to process all rules, do some simpler (faster) detection
      UChar32 c = input.char32At(0);
      icu::UnicodeString type = detect_type( c );
      if ( type == type_space ){
	return;
      }
      if ( doPunctFilter
	   && ( type == type_punctuation || type == type_currency ||
		type == type_emoticon || type == type_picto ) ) {
	if (tokDebug >= 2 ){
	  LOG << "   [tokenizeWord] skipped PUNCTUATION ["
			  << input << "]" << endl;
	}
	if ( !tokens.empty() && tokens.back().role & NOSPACE ){
	  tokens.back().role ^= NOSPACE;
	}
      }
      else {
	icu::UnicodeString word = input;
	if ( norm_set.find( type ) != norm_set.end() ){
	  word = "{{" + type + "}}";
	}
	Token T( type, word, space ? NOROLE : NOSPACE, lang );
	tokens.push_back( T );
	if (tokDebug >= 2){
	  LOG << "   [tokenizeWord] added token " << T << endl;
	}
      }
    }
    else {
      bool a_rule_matched = false;
      for ( const auto& rule : settings[lang]->rules ) {
	if ( tokDebug >= 4){
	  LOG << "\tTESTING " << rule->id << endl;
	}
	icu::UnicodeString type = rule->id;
	//Find first matching rule
	icu::UnicodeString pre, post;
	vector<icu::UnicodeString> matches;
	if ( rule->matchAll( input, pre, post, matches ) ){
	  a_rule_matched = true;
	  if ( tokDebug >= 4 ){
	    LOG << "\tMATCH: " << type << endl;
	    LOG << "\tpre=  '" << pre << "'" << endl;
	    LOG << "\tpost= '" << post << "'" << endl;
	    int cnt = 0;
	    for ( const auto& m : matches ){
	      LOG << "\tmatch[" << ++cnt << "]=" << m << endl;
	    }
	  }
	  if ( recurse
	       && ( type == type_word
		    || ( pre.isEmpty()
			 && post.isEmpty() ) ) ){
	    // so only do this recurse step when:
	    //   OR we have a WORD
	    //   OR we have an exact match of the rule (no pre or post)
	    if ( assigned_type != type_word ){
	      // don't change the type when:
	      //   it was already non-WORD
	      if ( tokDebug >= 4 ){
		LOG << "\trecurse, match didn't do anything new for " << input << endl;
	      }
	      tokens.push_back( Token( assigned_type, input, space ? NOROLE : NOSPACE, lang ) );
	      return;
	    }
	    else {
	      if ( tokDebug >= 4 ){
		LOG << "\trecurse, match changes the type:"
				<< assigned_type << " to " << type << endl;
	      }
	      tokens.push_back( Token( type, input, space ? NOROLE : NOSPACE, lang ) );
	      return;
	    }
	  }
	  if ( pre.length() > 0 ){
	    if ( tokDebug >= 4 ){
	      LOG << "\tTOKEN pre-context (" << pre.length()
			      << "): [" << pre << "]" << endl;
	    }
	    tokenizeWord( pre, false, lang ); //pre-context, no space after
	  }
	  if ( matches.size() > 0 ){
	    int max = matches.size();
	    if ( tokDebug >= 4 ){
	      LOG << "\tTOKEN match #=" << matches.size() << endl;
	    }
	    for ( int m=0; m < max; ++m ){
	      if ( tokDebug >= 4 ){
		LOG << "\tTOKEN match[" << m << "] = " << matches[m]
		    << " Space=" << (space?"TRUE":"FALSE") << endl;
	      }
	      if ( doPunctFilter
		   && (&rule->id)->startsWith("PUNCTUATION") ){
		if (tokDebug >= 2 ){
		  LOG << "   [tokenizeWord] skipped PUNCTUATION ["
				  << matches[m] << "]" << endl;
		}
		if ( !tokens.empty()
		     && tokens.back().role & NOSPACE ){
		  tokens.back().role ^= NOSPACE;
		}
	      }
	      else {
		bool internal_space = space;
		if ( post.length() > 0 ) {
		  internal_space = false;
		}
		else if ( m < max-1 ){
		  internal_space = false;
		}
		icu::UnicodeString word = matches[m];
		if ( norm_set.find( type ) != norm_set.end() ){
		  word = "{{" + type + "}}";
		  tokens.push_back( Token( type, word, internal_space ? NOROLE : NOSPACE, lang ) );
		}
		else {
		  if ( recurse ){
		    tokens.push_back( Token( type, word, internal_space ? NOROLE : NOSPACE, lang ) );
		  }
		  else {
		    tokenizeWord( word, internal_space, lang, type );
		  }
		}
	      }
	    }
	  }
	  else if ( tokDebug >=4 ){
	    // should never come here?
	    LOG << "\tPANIC there's no match" << endl;
	  }
	  if ( post.length() > 0 ){
	    if ( tokDebug >= 4 ){
	      LOG << "\tTOKEN post-context (" << post.length()
			      << "): [" << post << "]" << endl;
	    }
	    tokenizeWord( post, space, lang );
	  }
	  break;
	}
      }
      if ( !a_rule_matched ){
	// no rule matched
	if ( tokDebug >=4 ){
	  LOG << "\tthere's no match at all" << endl;
	}
	tokens.push_back( Token( assigned_type, input, space ? NOROLE : NOSPACE , lang ) );
      }
    }
  }

  bool TokenizerClass::init( const string& fname, const string& tname ){
    LOG << "Initiating tokeniser..." << endl;
    Setting *set = new Setting();
    if ( !set->read( fname, tname, tokDebug, theErrLog ) ){
      LOG << "Cannot read Tokeniser settingsfile " << fname << endl;
      LOG << "Unsupported language? (Did you install the uctodata package?)"
	  << endl;
      return false;
    }
    else {
      settings["default"] = set;
      default_language = "default";
    }
    if ( tokDebug ){
      LOG << "effective rules: " << endl;
      for ( size_t i=0; i < set->rules.size(); ++i ){
	LOG << "rule " << i << " " << *(set->rules[i]) << endl;
      }
      LOG << "EOS markers: " << set->eosmarkers << endl;
      LOG << "Quotations: " << set->quotes << endl;
      LOG << "Filter: " << set->filter << endl;
    }
    return true;
  }

  bool TokenizerClass::init( const vector<string>& languages,
			     const string& tname ){
    if ( tokDebug > 0 ){
      LOG << "Initiating tokeniser from language list..." << endl;
    }
    Setting *default_set = 0;
    for ( const auto& lang : languages ){
      if ( tokDebug > 0 ){
	LOG << "init language=" << lang << endl;
      }
      string fname = "tokconfig-" + lang;
      Setting *set = new Setting();
      string add;
      if ( default_set == 0 ){
	add = tname;
      }
      if ( !set->read( fname, add, tokDebug, theErrLog ) ){
	LOG << "problem reading datafile for language: " << lang << endl;
	LOG << "Unsupported language (Did you install the uctodata package?)"
	    << endl;
      }
      else {
	if ( default_set == 0 ){
	  default_set = set;
	  settings["default"] = set;
	  default_language = lang;
	}
	settings[lang] = set;
      }
    }
    if ( settings.empty() ){
      cerr << "ucto: No useful settingsfile(s) could be found." << endl;
      return false;
    }
    return true;
  }

}//namespace
