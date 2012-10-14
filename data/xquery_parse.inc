

{ TXQTerm }

function TXQTerm.debugTermToString: string;
var
  i: Integer;
begin
  result := '(' + ClassName;
  if length(children) >= 1 then result += ': '+children[0].debugTermToString();
  for i:= 1 to high(children) do result += ', '+children[1].debugTermToString();
  result += ')';
end;

destructor TXQTerm.destroy;
var
  i: Integer;
begin
  for i := 0 to high(children) do children[i].free;
  inherited destroy;
end;

procedure TXQTerm.push(t: TXQTerm);
begin
  SetLength(children, length(children) + 1);
  children[high(children)] := t;
end;

function TXQTerm.push(t: array of TXQTerm): TXQTerm;
var
  i: Integer;
begin
  for i := 0 to high(t) do push(t[i]);
  result := self;
end;

procedure TXQTerm.raiseParsingError(const s: string);
begin
  raise Exception.Create(s);
end;

procedure TXQTerm.raiseEvaluationError(const s: string);
begin
  raise EXQEvaluationException.Create(s);
end;

procedure TXQTerm.evaluateChildren(const context: TEvaluationContext; out results: TXQVArray);
var
  i: Integer;
begin
  setlength(results, length(children));
  for i:=0 to high(children) do
    results[i] := children[i].evaluate(context);
end;

function TXQTerm.toQueryCommand: TXQPathMatchingStep;
begin
  result.typ:=qcFunctionSpecialCase;
  result.specialCase:=self;
end;

procedure TXQTerm.addToQueryList(var path: TXQPathMatching);
begin
  SetLength(path, length(path) + 1);
  path[high(path)] := toQueryCommand;
end;

type

{ TXQParsingContext }

 TXQParsingContext = class
  AllowVariableUseInStringLiterals: boolean;
  AllowObjects: boolean;
  StripBoundarySpace: boolean;
  parsingModel: TXQParsingModel;
  encoding: TEncoding;

  str: string;
  pos: pchar;

  procedure raiseParsingError(s: string);
  procedure raiseInvalidModel(s: string);

  procedure requireXQuery(s: string = '');

  procedure skipWhitespace();
  procedure skipComment();
  procedure skipWhitespaceAndComment();
  procedure expect(c: char);
  procedure expect(s: string);
  function nextToken(lookahead: boolean=false): string;


  function parseSequenceLike(target: TXQTerm): TXQTerm;
  function parseQNameContinuation: string;
  function parseSequenceType(): TXQTermSequenceType;
  function replaceEntities(s: string; enc: TEncoding): string;
  function parseString(const w: string): TXQTerm;

  function parseFlower(akind: string): TXQTermFlower;
  function parseDirectConstructor(name: string): TXQTermConstructor;
  function parseComputedConstructor(name: string): TXQTermConstructor;

  //**Parses the next complete value@br
  //**i.e. it will parse from the left, until it obtains a value that can be evaluated.
  //**from @code(1 + 2) it will parse @code(1) (since @code(1) is complete), from @code(function(1,2,3)) it will parse everything (since @code(function) can not be evaluated without its parameters))@br
  //**(observation: that kind of looks like it parses everything except binary operators)
  function parseValue: TXQTerm;
  function parse: TXQTerm;
  function parsePrimaryLevel: TXQTerm;
end;



procedure TXQParsingContext.raiseParsingError(s: string);
begin
  if (pos < @str[1]) then pos := @str[1]; //make sure pos points to str
  if (pos > @str[length(str)]) then pos := @str[1];
  raise Exception.Create(s+#13#10'in: '+strslice(@str[1],pos-1)+' [<- error occurs before here] '+strslice(pos,@str[length(str)]));
end;

procedure TXQParsingContext.raiseInvalidModel(s: string);
begin
  raiseParsingError('This language feature is not available in the selected language. '+s);
end;

procedure TXQParsingContext.requireXQuery(s: string);
begin
  if parsingModel <> xqpmXQuery1 then raiseInvalidModel('XQuery is required '+s);
end;

procedure TXQParsingContext.skipWhitespace;
begin
  while pos^ in WHITE_SPACE do pos += 1;
end;

procedure TXQParsingContext.skipComment;
var nestene: integer;
begin
  nestene:=0;
  while pos^ <> #0 do begin
    if (pos^ = ':') and ((pos+1)^ = ')') then begin
      pos+=2;
      nestene-=1;
      if nestene=0 then exit;
    end else if (pos^ = '(') and ((pos+1)^ = ':') then begin
      pos+=2;
      nestene+=1;
    end else pos+=1;
  end;
end;

procedure TXQParsingContext.skipWhitespaceAndComment;
begin
  while (pos^ in WHITE_SPACE) or ((pos^ = '(') and ((pos+1)^ = ':')) do begin
    while pos^ in WHITE_SPACE do pos+=1;
    if (pos^ = '(') and ((pos+1)^ = ':') then
      skipComment();
  end;
end;

procedure TXQParsingContext.expect(c: char);
begin
  skipWhitespaceAndComment;
    if pos^ <> c then
      raise Exception.Create('"'+c+'"'+' expected, but "'+pos^+'" found'#13#10+strslice(@str[1],pos));
    pos+=1;
end;

procedure TXQParsingContext.expect(s: string);
var
   i: Integer;
begin
  skipWhitespaceAndComment;
  for i:=1 to length(s) do begin
    if pos^ <> s[i] then
      raise Exception.Create('"'+ s+'" expected, but "'+pos^+'" found'#13#10+strslice(@str[1],pos));
    pos+=1;
  end;
end;

//read the next token ('string', number: (-?[0-9]+|[0-9]*.[0-9]+|[0-9]+.[0-9]*)([eE][+-]?[0-9]+)?, symbol, identifier)
function TXQParsingContext.nextToken(lookahead: boolean=false): string;
const SYMBOLS = ['''','"', '(','=','!','<','>',')',',','[',']','/','|','+','*','{','}', '?']; // $ and ; are no symbols
const START_SYMBOLS = ['-'];
var start:pchar;
   numberE, numberPoint: boolean;
begin
  skipWhitespaceAndComment;
  if pos^ = #0 then exit('');
  start:=pos;
  if pos^='''' then begin
    repeat
      pos+=1;
      if (pos^ = '''') then
        if ((pos+1)^ <> '''') then break
        else pos+=1;
    until pos^ in [#0];
    pos+=1;
  end else if pos^='"' then begin
    repeat
      pos+=1;
      if (pos^ = '"') then
        if ((pos+1)^ <> '"') then break
        else pos+=1;
    until pos^ in [#0];
    pos+=1;
  end else if pos^ in (SYMBOLS + START_SYMBOLS) then begin
    if lookahead then exit(pos^)
    else begin
      pos+=1;
      exit((pos-1)^);
    end;
  end else if (pos^ in ['0'..'9','-']) or ((pos^ = '.') and ((pos+1)^ in ['0'..'9','e','E'])) then begin
    numberE:=false;
    numberPoint:=pos^ = '.';
    repeat
      pos+=1;
      if pos^ = '.' then begin
        if numberPoint then raiseParsingError('Double . in number');
        numberPoint:=true;
        pos+=1;
      end;
      if pos^ in ['e','E'] then begin
        if numberE then raiseParsingError('Double e in number');
        pos+=1;
        numberE:=true;
        if not (pos^ in ['0'..'9','+','-']) then raiseParsingError('Invalid character after e in number')
        else pos+=1;
      end;
    until not (pos^ in ['0'..'9']);
  end else if (pos^ = '@') and ((pos+1)^='*') then begin
    pos+=2;
  end else if (pos^ = ':') and ((pos+1)^='=') then begin
    pos+=2;
  end else begin
    repeat
      pos+=1;
    until (pos^ in SYMBOLS + WHITE_SPACE + [#0]) or ((pos^ = ':') and ((pos+1)^='='));
  end;
  assert(start<pos);
  result:=strslice(start,pos-1);
  if lookahead then pos:=start;
end;

function TXQParsingContext.parseSequenceLike(target: TXQTerm): TXQTerm;
var
  t: String;
begin
  result := target;
  skipWhitespaceAndComment();
  if pos^ = ')' then begin expect(')'); exit(); end;
  result.push(parse());
  t := nextToken();
  while t = ',' do begin
    result.push(parse());
    t := nextToken();
  end;
  if t <> ')' then raiseParsingError('Expected closing parenthesis: )');
end;

function TXQParsingContext.parseQNameContinuation: string;
begin
  if (pos^ = ':') and (((pos - 1)^ = '*') or ((pos + 1)^ = '*')) then result := nextToken()
  else if ((pos - 1)^ = ':') and (((pos)^ = '*') or ((pos - 1)^ = '*')) then result := nextToken()
  else result := '';
end;

function isKindTestFunction(const word: string): boolean;  //Lookahead to recognize KindTest of the XPath-EBNF
begin
  result := (word = 'text') or (word = 'node') or (word = 'comment') or (word = 'processing-instruction')
            or (word = 'element') or (word = 'document-node') or (word = 'schema-element') or (word = 'attribute') or (word = 'schema-attribute');
end;

function TXQParsingContext.parseSequenceType: TXQTermSequenceType;
var word, sequenceType: string;
  index: Integer;
begin
  word := nextToken();

  result := TXQTermSequenceType.Create();
  result.allowNone:=false;
  result.allowMultiple:=false;

  result.name:=word;
  if (isKindTestFunction(word)) or (word = 'empty-sequence') or (word = 'item') then begin
    expect('(');
    parseSequenceLike(result);
    if (word = 'empty-sequence') then begin
      result.kind:=tikNone;
      if length(result.children) <> 0 then raiseParsingError('invalid sequence type');
      exit
    end else if word = 'item' then begin
      result.kind:=tikAny;
      if length(result.children) <> 0 then raiseParsingError('invalid sequence type');
    end else begin
       result.kind:=tikElementTest;
       result.nodeMatching := convertElementTestToPathMatchingStep(word, result.children);
    end;
  end else begin;
    if strBeginsWith(word, 'xs:') then delete(word, 1, 3);

    result.kind:=tikAtomic;
    index := types.IndexOf(word);
    if index < 0 then raise Exception.Create('Unknown type: '+word);
    result.atomicTypeInfo := TXQValueClass(types.Objects[index]);
  end;


  word := nextToken(true);
  if (length(word) = 1) and (word[1] in ['?', '*', '+']) then begin
    case word[1] of
      '?': result.allowNone:=true;
      '+': result.allowMultiple:=true;
      '*': begin result.allowNone:=true; result.allowMultiple:=true; end;
    end;
    pos+=1;
  end;
end;

function TXQParsingContext.parseFlower(akind: string): TXQTermFlower;
  function parseVariableName: string;
  begin
    Result := nextToken;
    if result = '' then raiseParsingError('Expected variable name');
    if result[1] <> '$' then raiseParsingError('Expected variable name starting with $, but got "'+result+'"');
    delete(result, 1, 1);
  end;

  procedure parseVarBinding;
  var temp: string;
  begin
    setlength(result.vars, length(result.vars) + 1);
    with result.vars[high(result.vars)] do begin
      if akind = 'for' then kind := xqfkFor
      else if akind = 'let' then kind := xqfkLet
      else raiseParsingError('Invalid flower: '+akind);

      if (kind = xqfkLet) and (parsingModel = xqpmXPath2) then raiseInvalidModel('let is not supported in XPath 2.0');

      varname := parseVariableName;

      temp := nextToken;
      if temp = 'as' then begin
        requireXQuery;
        sequenceTyp := parseSequenceType;
        temp := nextToken;
      end else sequenceTyp := nil;

      if kind = xqfkFor then begin

        {if temp = 'allowing' then begin
          requireXQuery3;
          allowingEmpty := nextToken;
          temp := nextToken;
        end;}

        if temp = 'at' then begin
          requireXQuery;
          positionVarname := parseVariableName;
          temp := nextToken;
        end;

        if temp <> 'in' then raiseParsingError('Expected "in".')
      end else
        if temp <> ':=' then raiseParsingError('Expected ":=".');

      expr := parse();
    end;
  end;

var
  token: String;

  procedure parseOrderSpecAndNextToken;
  begin
    setlength(result.orders, length(result.orders) + 1);
    with result.orders[high(result.orders)] do begin
      expr := parse();
      token := nextToken;

      if (token = 'ascending') or (token = 'descending') then begin
        descending := token = 'descending';
        token := nextToken;
      end;

      emptyOrder := xqfoStatic;
      if token = 'empty' then begin
        token := nextToken;
        if token = 'greatest' then emptyOrder := xqfoEmptyGreatest
        else if token = 'least' then emptyOrder := xqfoEmptyLeast
        else raiseParsingError('Expected "greatest" or "least"');
        token := nextToken;
      end;

      if token = 'collation' then begin
        collation := nextToken;
        if (collation = '') or not (collation[1] in ['''', '"']) or (collation[1] <> collation[length(collation)]) then raiseParsingError('Invalid collation');
        collation := copy(collation, 2, length(collation) - 2);
        token := nextToken;
      end
    end;
  end;
begin
  result := TXQTermFlower.Create;
  parseVarBinding;

  token := nextToken();

  while (token = ',') or (token = 'for') or (token = 'let') do begin
    if token <> ',' then begin
      requireXQuery;
      akind := token;;
    end;
    parseVarBinding;
    token := nextToken();
  end;

  if token <> 'return' then begin
    requireXQuery;

    if token = 'where' then begin
      result.where := parse;
      token := nextToken();
    end;

    if (token = 'stable') or (token = 'order') then begin
      if token = 'stable' then expect('order'); //always be stable
      expect('by');
      repeat
        parseOrderSpecAndNextToken;
      until token <> ',';
      if token <> 'return' then raiseParsingError('Expected return');
    end;
  end;

  result.returned := parse;
end;

function TXQParsingContext.parseDirectConstructor(name: string): TXQTermConstructor;
  procedure parseCommonContent(parent: TXQTermConstructor; border: char);
    procedure pushElementContent(s: string);
    begin
      if length(s) = 0 then exit;
      parent.push(TXQTermString.create(s));
    end;
    function strIsWhitespace(p: pchar; l: longint): boolean;
    begin
      if l = 0 then exit(true);
      strlTrimRight(p, l, WHITE_SPACE);
      result := l = 0;
    end;

  var marker: PChar;
    atBoundary: Boolean;
  begin
    atBoundary := (border = '<') and StripBoundarySpace;
    while pos^ <> #0 do begin
      marker := pos;
      while not (pos^ in [#0, '{', '}', '&']) and (pos^ <> border) do pos+=1;

      if pos^ = #0 then raiseParsingError('Unexpected end');

      if not atBoundary or (pos^ in ['}', '&']) or not strIsWhitespace(marker, pos - marker) then begin
        pushElementContent(strFromPchar(marker, pos - marker));
        atBoundary := false;
      end;

      if pos^ = border then begin
        if (pos^ in ['''', '"']) and ((pos+1)^ = border) then begin
          pushElementContent(border);
          pos+=2;
          continue;
        end else exit;
      end;

      case pos^ of
        '{': if (pos+1)^ <> '{' then begin
          pos += 1;
          parent.push(parsePrimaryLevel);
          expect('}');
          atBoundary := (border = '<') and StripBoundarySpace;
        end else begin
          pushElementContent('{');
          pos+=2;
        end;
        '}': begin
          if (pos+1)^ <> '}' then raiseParsingError('Invalid closing parenthesis');
          pushElementContent('}');
          atBoundary:=false;
          pos += 2;
        end;
        '&': begin
          marker := pos;
          while not (pos^ in [#0, ';']) do pos += 1;
          if pos^ = #0 then raiseParsingError('Unexpected end');
          pos += 1;
          pushElementContent(replaceEntities(strFromPchar(marker, pos - marker), encoding));
          atBoundary:=false;
        end;
      end;
    end;
  end;

  function parsePIConstructor: TXQTermConstructor;
  var marker: PChar;
  begin
    result := TXQTermConstructor.create(tetProcessingInstruction, txqtermstring.create(nextToken()));
    skipWhitespace();
    marker := pos;
    while (pos^ <> #0) and ((pos^ <> '?') or ((pos+1)^ <> '>')) do pos+=1;
    result.push(txqtermstring.create(strFromPchar(marker, pos - marker)));
    if pos^ = #0 then raiseParsingError('Unexpected end');
    expect('?>');
  end;

  function parseCommentConstructor: TXQTermConstructor;
  var marker: PChar;
  begin
    expect('--');
    marker := pos;
    while (pos^ <> #0) and ((pos^ <> '-') or ((pos+1)^ <> '-')) do pos+=1;
    if pos^ = #0 then raiseParsingError('Unexpected end');
    result := TXQTermConstructor.create(tetComment, txqtermstring.create(strFromPchar(marker, pos - marker)));
    expect('-->');
  end;

var
  token: String;
  marker: PChar;
  attribute: TXQTermConstructor;
begin
  name := nextToken();
  if name = '!' then exit(parseCommentConstructor)
  else if name = '?' then exit(parsePIConstructor);
  result := TXQTermConstructor.create(tetOpen, TXQTermString.create(name));
  token := nextToken();
  while (token <> '>') and (token <> '/') and (token <> '') do begin
    attribute := TXQTermConstructor.create(tetAttributeName, TXQTermString.create(token));
    result.push(attribute);
    expect('=');
    skipWhitespaceAndComment();
    if not (pos^ in ['''', '"']) then raiseParsingError('Expected attribute value');
    marker := pos;
    pos+=1;
    parseCommonContent(attribute, marker^);
    expect(marker^);
    token := nextToken();
  end;
  if token = '' then raiseParsingError('Attribute expected');
  if token = '/' then begin
    expect('>');
    exit;
  end;

  while pos^ <> #0 do begin
    parseCommonContent(result, '<');

    if pos^ = #0 then raiseParsingError('Unexpected end');
    if pos^ = '<' then begin
      pos += 1;
      case pos^ of
        '/': begin
          pos += 1;
          if nextToken <> name then raiseParsingError('Expected matching closing tag');
          expect('>');
          exit;
        end;
        '!': if strBeginsWith(pos, '![CDATA[') then begin
          pos += length('![CDATA[');
          marker := pos;
          while (pos^ <> #0) and not strBeginsWith(pos, ']]>') do pos+=1;
          if pos^ = #0 then raiseParsingError('Unexpected end');
          result.push(TXQTermString.create(strFromPchar(marker, pos - marker)));
          pos+=3;
        end else if strBeginsWith(pos, '!--') then begin
          pos += 1;
          result.push(parseCommentConstructor);
        end else raiseParsingError('Invalid character combination after <!');
        '?': begin
          pos += 1;
          result.push(parsePIConstructor);
        end;
        else result.push(parseDirectConstructor('<'));
      end;
    end;
  end;
  raiseParsingError('Unexpected end')
end;

function TXQParsingContext.parseComputedConstructor(name: string): TXQTermConstructor;
var
  token: String;
  tempSeq: TXQTerm;
  onlyConstructors: Boolean;
  i: Integer;
  expectName: Boolean;
begin
  token := name;
  if (token = 'element') or (token = 'document') then result := TXQTermConstructor.create(tetOpen)
  else if (token = 'attribute') then result := TXQTermConstructor.create(tetAttributeName)
  else if (token = 'text') then result := TXQTermConstructor.create(tetText)
  else if (token = 'processing-instruction') then result := TXQTermConstructor.create(tetProcessingInstruction)
  else if (token = 'comment') then result := TXQTermConstructor.create(tetComment)
  else raiseParsingError('Unknown constructor name');
  expectName := (result.typ in [tetOpen, tetProcessingInstruction, tetAttributeName]) and (token <> 'document');
  if expectName then begin
    token := nextToken();
    if token = '{' then begin
      result.nameValue := parse;
      expect('}');
    end else result.nameValue := TXQTermString.create(token);
  end;
  expect('{');
  skipWhitespaceAndComment();
  if pos^ <> '}' then begin
    if result.typ in [tetOpen, tetProcessingInstruction, tetAttributeName] then begin
      tempSeq := parsePrimaryLevel;
      if tempSeq is TXQTermSequence then begin
        onlyConstructors := true;
        for i:= 0 to high(tempseq.children) do
          if not (tempSeq.children[i] is TXQTermConstructor) then begin
            onlyConstructors:=false;
            break;
          end;
        if onlyConstructors then begin
          result.children := tempseq.children;
          tempseq.children := nil;
          tempSeq.free;
        end else result.push(tempSeq); //that's really slow for nodes because it makes a deep copy of them if they are taken from a subsequence. But if it's mixing atomic/nodes flattening the sequences makes the separator spaces wrong
      end else result.push(tempSeq);
    end else result.nameValue := parsePrimaryLevel;
  end else if not expectName then
    raiseParsingError('This type of node must not be empty ');
  expect('}');
end;

function TXQParsingContext.replaceEntities(s: string; enc: TEncoding): string;
var
  n, p: Integer;
  temp: string;
begin
  p := 1;
  n := strIndexOf(s, '&');
  while n > 0 do begin
    result += copy(s, p, n - p);
    p := n + 1;
    n := strIndexOf(s, ';', p);
    temp := copy(s, p, n - p);
    case temp of
      'lt': result += '<';
      'gt': result += '>';
      'amp': result += '&';
      'quot': result += '"';
      'apos': result += '''';
      else begin
        if temp[1] <> '#' then raiseParsingError('Invalid entity');
        delete(temp,1,1);
        if temp[1] = 'x' then result += strGetUnicodeCharacter(StrToInt('$'+strcopyfrom(temp,2)), enc)
        else result += strGetUnicodeCharacter(StrToInt(temp), enc);
      end;
    end;
    p := n + 1;
    n := strIndexOf(s, '&', n);
  end;
  result += strcopyfrom(s, p);
end;

function TXQParsingContext.parseString(const w: string): TXQTerm;
  var s: string;
      lastStrStart, i: integer;
  begin
    s := StringReplace(copy(w,2,length(w)-2), w[1]+w[1], w[1], [rfReplaceAll]);
    if parsingModel in [xqpmXQuery1] then s := replaceEntities(s, encoding);
    if (not AllowVariableUseInStringLiterals) or (w[1] = '''') or (system.Pos('$', s) <= 0)  then
      exit(TXQTermString.create(s));

    result := TXQTermNamedFunction.create('concat');
    i := 1;
    lastStrStart := 1;
    while  i <= length(s) do begin
      if s[i] = '$' then begin //Read variable
        if lastStrStart < i then result.push(TXQTermString.create(copy(s, lastStrStart, i-lastStrStart)));
        if i >= length(s) - 1 then raiseParsingError('Variable without name or ; at end of '+s);
        lastStrStart:=i;
        while (i <= length(s)) and not (s[i] in [';',' ',#9,#13,#10]) do i+=1;
        if i > length(s) then raiseParsingError('Variable without terminating ; in '+s);
        if lastStrStart  = i then raiseParsingError('Variable without name ($;) in '+s);
        result.push(TXQTermVariable.create(copy(s, lastStrStart, i-lastStrStart)));
        i+=1;
        lastStrStart:=i;
      end else i+=1;
    end;
    if lastStrStart < length(str) then result.push(TXQTermString.Create(copy(s, lastStrStart, i - lastStrStart)));
  end;

function TXQParsingContext.parseValue: TXQTerm;
var
  word: String;
  wordlookahead: String;
  temp: PChar;
  constr: Boolean;
begin
  result := nil;
  word:=nextToken();
  if word='' then exit();
  case word[1] of
    '''', '"':  exit(parseString(word));
    '$': begin
      if length(word) < 2 then raiseParsingError('Variable must have at least 2 characters: '+word);
      exit(TXQTermVariable.Create(word));
    end;
    '@': exit(TXQTermReadAttribute.create(strcopyfrom(word,2) + parseQNameContinuation));
    '+', '-': begin
      wordlookahead:=nextToken(true);
      if (word = '-') and
           ((wordlookahead[1] in ['0'..'9'])
             or ((length(wordlookahead) > 2) and (wordlookahead[1] = '.') and (wordlookahead[2] in ['0'..'9']))) then begin
        expect(wordlookahead);
        exit(TXQTermNumber.Create('-' + wordlookahead));
      end else
        exit(TXQTermUnaryOp.create(word, parseValue()));
    end;

    '(': exit(parseSequenceLike(TXQTermSequence.Create)); //only sequence or priority brackets

    '*': exit(TXQTermNodeMatcher.Create(word + parseQNameContinuation));
    '/': begin
      if pos^ = '/' then begin expect('/'); word+='/';end;
      skipWhitespaceAndComment();
      if pos^ in [#0,',',')',']'] then exit(TXQTermNodeMatcher.Create('/')) //leading lone slash (see standard#parse-note-leading-lone-slash)
      else exit(TXQTermBinaryOp.Create(word, TXQTermNodeMatcher.Create('/'), parseValue()));
    end;

    '0'..'9': exit(TXQTermNumber.Create(word));
    '.': if word = '.' then exit(TXQTermNamedFunction.Create('.'))
         else if word = '..' then exit(TXQTermNodeMatcher.Create(word))
         else if word[2] in ['0'..'9', 'e', 'E'] then exit(TXQTermNumber.Create(word))
         else raiseParsingError('Unknown term: '+word);

    '<': begin
      requireXQuery('to use constructors (this error can also mean an invalid < )');
      result := parseDirectConstructor(word);
    end



    else begin
      wordlookahead := nextToken(true);
      if wordlookahead='::' then word+=nextToken();
      if strEndsWith(word,'::') then begin
        word+=nextToken();
        wordlookahead := nextToken(true);
      end;
      if ((wordlookahead = '*') and strEndsWith(word, ':') and not strEndsWith(word, '::'))
         or (strEndsWith(word, '::*') and strBeginsWith(wordlookahead, ':')) then begin
        word += nextToken();
        wordlookahead := nextToken(true);
      end;


      if wordlookahead = '(' then begin
        expect('(');
        if word = 'if' then begin
          result := TXQTermIf.Create();
          result.push(parsePrimaryLevel);
          expect(')'); expect('then');
          result.push(parse());
          expect('else');
          result.push(parse());
          exit;
        end;

        if word = 'typeswitch' then begin
          requireXQuery('for typeswitch statement');
          result := TXQTermTypeSwitch.Create;
          result.push(parsePrimaryLevel());
          expect(')');

          word := nextToken();
          while word = 'case' do begin
            skipWhitespaceAndComment();
            result.push(TXQTermSequence.Create);
            if pos^ = '$' then begin result.children[high(result.children)].push(TXQTermVariable.create(nextToken())); expect('as'); end;
            result.children[high(result.children)].push(parseSequenceType());
            expect('return');
            result.children[high(result.children)].push(parse());
            word := nextToken();
          end;
          if word <> 'default' then raiseParsingError('expected "default" clause');
          skipWhitespaceAndComment();
          result.push(TXQTermSequence.Create);
          if pos^ = '$' then result.children[high(result.children)].push(TXQTermVariable.create(nextToken()));
          expect('return');
          result.children[high(result.children)].push(parse());

          exit;
        end;

        result := TXQTermNamedFunction.createIfExists(word, strBeginsWith(word, 'op:'));
        if (result = nil) and
           (isKindTestFunction(word)
           or (strContains(word, '::') and (isKindTestFunction(strCopyFrom(word, strIndexOf(word, '::') + 2))))) then begin
          if strContains(word, 'attribute') then result := TXQTermReadAttribute.create(word, true)
          else result := TXQTermNodeMatcher.Create(word, true);
        end;
        if result = nil then raise Exception.Create('unknown function: '+word);
        exit(parseSequenceLike(result));
      end;


      if (wordlookahead<>'') and (wordlookahead[1]='$') then begin
        if (word = 'for') or (word = 'let') then
          exit(parseFlower(word));

        if (word = 'some') or (word = 'every') then begin
          result := TXQTermSomeEvery.Create(word = 'every');
          result.push(TXQTermVariable.Create(nextToken())); expect('in'); result.push(parse());
          word := nextToken();
          while word = ',' do begin
            result.push(TXQTermVariable.Create(nextToken())); expect('in'); result.push(parse());
            word := nextToken();
          end;
          Assert(word = 'satisfies');
          result.push(parse());
          exit;
        end;
      end;


      if strBeginsWith(word,'attribute::') then
        exit(TXQTermReadAttribute.Create(strcopyfrom(word,length('attribute::')+1)));



      if (word = 'element') or (word = 'attribute') or (word = 'document') or (word = 'text') or (word = 'processing-instruction') or (word = 'comment') then begin
        constr := wordlookahead = '{';
        if not constr then begin //look for name (this will allow something like text name {...} here, but that's going to raise an error later anyways)
          temp := pos;
          nextToken();
          if nextToken() = '{' then constr := true;
          pos := temp;
        end;
        if constr then begin
          requireXQuery('to use constructors');
          exit(parseComputedConstructor(word));
        end;
      end;

      if wordlookahead = '{' then begin
        if (word = 'unordered') or (word = 'ordered') then begin //TODO: actually use that
          requireXQuery();
          expect('{'); result := parse(); expect('}');
          exit;
        end;
      end;

      if (word = 'validate') and ((wordlookahead = 'lax') or (wordlookahead = 'strict')) then begin
        requireXQuery('for schema validation');
        expect(wordlookahead);
        expect('{');
        raiseParsingError('Schema validaton is not supported');
      end;




      result := TXQTermNodeMatcher.Create(word);
    end;
  end;
end;

type PXQTerm = ^TXQTerm;

//searchs the term to split
//e.g.
//          to                          to
//        1     +        * 4      =>  1     +
//            2    3                      2    3 <- * 4
//takes a pointer to a txqterm, since it sets the variable that should be changed
//(parentheses can be ignored since they are stored as sequence terms in the tree)
function ripBinOpApart(term: pxqterm; const prio: integer): PXQTerm;
begin
  if not (term^ is TXQTermBinaryOp) or (TXQTermBinaryOp(term^).operatorInfo^.priority >= prio) then result := term
  else result := ripBinOpApart(@term^.children[1], prio);
end;

function TXQParsingContext.parse: TXQTerm;
var astroot: TXQTerm;


  procedure pushBinaryOp(const v: string);
  var res: TXQTermBinaryOp;
      replace: PXQTerm;
  begin
    expect(v);

    res := TXQTermBinaryOp.Create(v);
    replace := ripBinOpApart(@astroot, res.operatorInfo^.priority);

    res.push(replace^);
    replace^ := res;

    if res.operatorInfo^.followedBy <> '' then begin
      expect(res.operatorInfo^.followedBy);
      res.push(parseSequenceType()); //assume we read instance of/cast/castable/treat as
    end else
      res.push(parseValue())
  end;

  var word: string;
    replace: PXQTerm;
begin
  astroot := parseValue();
  while true do begin
    word := nextToken(true);
    case word of
      '', ',', ')', ']', '}', 'else', 'return', 'satisfies', 'for', 'let', 'order', 'where', 'stable', 'ascending', 'descending', 'empty', 'collation', 'case', 'default':
        exit(astroot);
      //'=', '+', '-', '*': pushBinaryOp(word);
      '!': pushBinaryOp('!=');
      '<', '>': case (pos+1)^ of
        '<', '>': pushBinaryOp(word+word);
        '=': pushBinaryOp(word+'=');
        else pushBinaryOp(word);
      end;
      '/':
        if (pos+1)^ = '/' then
          pushBinaryOp('//')
        else pushBinaryOp('/');
      '[': begin
        expect('[');
        replace := ripBinOpApart(@astroot, 10000);
        replace^ := TXQTermFilterSequence.Create(replace^, parse());
        expect(']');
      end;
      ':=': begin expect(':='); exit(TXQTermDefineVariable.Create(astroot, parse())); end;
      else if binaryOps.IndexOf(word) >= 0 then pushBinaryOp(word)
      else if (word[1] = '.') and AllowObjects then begin
        expect(word);
        replace := ripBinOpApart(@astroot, 10000);
        if (replace^ is TXQTermFilterSequence) or (replace^ is TXQTermSequence) or (replace^ is TXQTermVariable) or (replace^ is TXQTermNamedFunction)  then
          replace^ := TXQTermReadObjectProperty.Create(strCopyFrom(word, 2)).push([replace^])
         else
          raiseParsingError('Unexpected .');
      end else
        raiseParsingError('Unknown or unexpected operator: '+word);
    end;
  end;
  result := astroot;
end;

function TXQParsingContext.parsePrimaryLevel: TXQTerm;
begin
  result := parse;
  if nextToken(true) = ',' then begin
    result := TXQTermSequence.Create.push([result]);
    while nextToken(true) = ',' do begin
      expect(',');
      result.push(parse);
    end;
  end;
end;
