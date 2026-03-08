<?xml version="1.0" encoding="UTF-8"?>
<!--
  db2adoc.xsl - DocBook 5 to AsciiDoc converter
  BRL-CAD

  Converts BRL-CAD DocBook 5 documentation to AsciiDoc format
  suitable for processing by asciiquack.

  Supported root elements:
    refentry  -> doctype: manpage
    article   -> doctype: article
    book      -> doctype: book

  Usage with xsltproc:
    xsltproc db2adoc.xsl input.xml > output.adoc
-->
<xsl:stylesheet version="1.0"
  xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:db="http://docbook.org/ns/docbook"
  xmlns:xl="http://www.w3.org/1999/xlink"
  exclude-result-prefixes="db xl">

  <xsl:output method="text" encoding="UTF-8" indent="no"/>
  <xsl:strip-space elements="*"/>
  <!-- preserve-space for elements with mixed content (inline text + elements):
       Adding db:para and its common inline containers ensures that inter-element
       whitespace (e.g. the space between <command>foo</command> <filename>bar</filename>)
       is not stripped before our text() template can handle it. -->
  <xsl:preserve-space elements="db:programlisting db:screen db:literallayout db:synopsis db:address
                                 db:para db:simpara db:title db:term db:phrase
                                 db:emphasis db:command db:option db:filename db:varname
                                 db:envar db:systemitem db:literal db:code db:constant
                                 db:type db:markup db:application db:function db:parameter
                                 db:replaceable db:userinput db:computeroutput db:prompt
                                 db:entry db:refpurpose db:citetitle db:link"/>

  <!-- ============================================================
       PARAMETERS
       ============================================================ -->
  <!-- Base section level offset (0 = start at ==) -->
  <xsl:param name="section-depth" select="0"/>

  <!-- ============================================================
       UTILITY TEMPLATES
       ============================================================ -->

  <!-- Emit N equal-signs for a section heading -->
  <xsl:template name="section-mark">
    <xsl:param name="level" select="1"/>
    <xsl:text>=</xsl:text>
    <xsl:if test="$level > 1">
      <xsl:call-template name="section-mark">
        <xsl:with-param name="level" select="$level - 1"/>
      </xsl:call-template>
    </xsl:if>
  </xsl:template>

  <!-- Count ancestor sections to determine heading level -->
  <xsl:template name="heading-level">
    <xsl:param name="node" select="."/>
    <!-- Each section/chapter/part nesting adds one level -->
    <xsl:value-of select="count($node/ancestor::db:section)
      + count($node/ancestor::db:refsection)
      + count($node/ancestor::db:refsect1)
      + count($node/ancestor::db:refsect2)
      + count($node/ancestor::db:chapter)
      + count($node/ancestor::db:appendix)
      + count($node/ancestor::db:preface)
      + count($node/ancestor::db:part)
      + 2"/>
  </xsl:template>

  <!-- Normalize inline whitespace: collapse runs of whitespace to single space -->
  <xsl:template name="normalize-text">
    <xsl:param name="text" select="."/>
    <xsl:value-of select="normalize-space($text)"/>
  </xsl:template>

  <!-- Escape AsciiDoc special characters in plain text context -->
  <xsl:template name="escape-adoc">
    <xsl:param name="text"/>
    <!-- For now, pass text through - most docbook text is safe in adoc -->
    <xsl:value-of select="$text"/>
  </xsl:template>

  <!-- Repeat a string N times -->
  <xsl:template name="repeat-string">
    <xsl:param name="str"/>
    <xsl:param name="n" select="1"/>
    <xsl:if test="$n > 0">
      <xsl:value-of select="$str"/>
      <xsl:call-template name="repeat-string">
        <xsl:with-param name="str" select="$str"/>
        <xsl:with-param name="n" select="$n - 1"/>
      </xsl:call-template>
    </xsl:if>
  </xsl:template>

  <!-- List item bullet prefix for given depth (0-based) -->
  <xsl:template name="list-bullet">
    <xsl:param name="depth" select="0"/>
    <xsl:call-template name="repeat-string">
      <xsl:with-param name="str" select="'*'"/>
      <xsl:with-param name="n" select="$depth + 1"/>
    </xsl:call-template>
  </xsl:template>

  <!-- List item number prefix for given depth (0-based) -->
  <xsl:template name="list-number">
    <xsl:param name="depth" select="0"/>
    <xsl:call-template name="repeat-string">
      <xsl:with-param name="str" select="'.'"/>
      <xsl:with-param name="n" select="$depth + 1"/>
    </xsl:call-template>
  </xsl:template>

  <!-- ============================================================
       BLOCK-IN-PARA SEPARATOR
       When a block-level element (table, figure, listing, list,
       admonition, etc.) is a direct child of db:para or db:simpara,
       a blank line must be emitted first so that any preceding inline
       text is closed as a proper AsciiDoc paragraph before the block
       attributes start.  Call this at the very start of every template
       that produces a block-level AsciiDoc construct.
       ============================================================ -->

  <xsl:template name="block-sep">
    <!-- If we're inside a para/simpara, ensure a blank line separates us
         from any preceding inline content on the same line. -->
    <xsl:if test="parent::db:para or parent::db:simpara">
      <xsl:text>&#10;&#10;</xsl:text>
    </xsl:if>
  </xsl:template>

  <!-- ============================================================
       ROOT ELEMENT DISPATCH
       ============================================================ -->

  <!-- Man page (refentry) -->
  <xsl:template match="/db:refentry">
    <xsl:call-template name="manpage-header"/>
    <xsl:apply-templates select="db:refnamediv"/>
    <xsl:apply-templates select="db:refsynopsisdiv"/>
    <xsl:apply-templates select="db:refsection | db:refsect1"/>
  </xsl:template>

  <!-- Article -->
  <xsl:template match="/db:article">
    <xsl:call-template name="article-header"/>
    <xsl:apply-templates/>
  </xsl:template>

  <!-- Book -->
  <xsl:template match="/db:book">
    <xsl:call-template name="book-header"/>
    <xsl:apply-templates/>
  </xsl:template>

  <!-- Bare refentry nested inside book/article -->
  <xsl:template match="db:refentry">
    <xsl:call-template name="manpage-header"/>
    <xsl:apply-templates select="db:refnamediv"/>
    <xsl:apply-templates select="db:refsynopsisdiv"/>
    <xsl:apply-templates select="db:refsection | db:refsect1"/>
  </xsl:template>

  <!-- ============================================================
       DOCUMENT HEADERS
       ============================================================ -->

  <xsl:template name="manpage-header">
    <!-- = COMMAND(SECTION)
         :doctype: manpage
         :manmanual: ...
         :mansource: BRL-CAD
    -->
    <xsl:variable name="title">
      <xsl:value-of select="normalize-space(db:refmeta/db:refentrytitle)"/>
    </xsl:variable>
    <xsl:variable name="section">
      <xsl:value-of select="normalize-space(db:refmeta/db:manvolnum)"/>
    </xsl:variable>
    <xsl:variable name="manual">
      <xsl:value-of select="normalize-space(db:refmeta/db:refmiscinfo[@class='manual'])"/>
    </xsl:variable>
    <xsl:variable name="source">
      <xsl:choose>
        <xsl:when test="db:refmeta/db:refmiscinfo[@class='source']">
          <xsl:value-of select="normalize-space(db:refmeta/db:refmiscinfo[@class='source'])"/>
        </xsl:when>
        <xsl:otherwise>BRL-CAD</xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <!-- Title line -->
    <xsl:text>= </xsl:text>
    <xsl:value-of select="$title"/>
    <xsl:text>(</xsl:text>
    <xsl:value-of select="$section"/>
    <xsl:text>)</xsl:text>
    <xsl:text>&#10;</xsl:text>
    <!-- doctype -->
    <xsl:text>:doctype: manpage&#10;</xsl:text>
    <!-- mansource -->
    <xsl:if test="$source != ''">
      <xsl:text>:mansource: </xsl:text>
      <xsl:value-of select="$source"/>
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
    <!-- manmanual -->
    <xsl:if test="$manual != ''">
      <xsl:text>:manmanual: </xsl:text>
      <xsl:value-of select="$manual"/>
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <xsl:template name="article-header">
    <xsl:variable name="title">
      <xsl:choose>
        <xsl:when test="db:info/db:title">
          <xsl:value-of select="normalize-space(db:info/db:title)"/>
        </xsl:when>
        <xsl:when test="db:title">
          <xsl:value-of select="normalize-space(db:title)"/>
        </xsl:when>
        <xsl:otherwise>Untitled</xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <xsl:text>= </xsl:text>
    <xsl:value-of select="$title"/>
    <xsl:text>&#10;</xsl:text>
    <xsl:call-template name="emit-authors">
      <xsl:with-param name="info" select="db:info"/>
    </xsl:call-template>
    <xsl:text>:doctype: article&#10;</xsl:text>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <xsl:template name="book-header">
    <xsl:variable name="title">
      <xsl:choose>
        <xsl:when test="db:info/db:title">
          <xsl:value-of select="normalize-space(db:info/db:title)"/>
        </xsl:when>
        <xsl:when test="db:title">
          <xsl:value-of select="normalize-space(db:title)"/>
        </xsl:when>
        <xsl:otherwise>Untitled</xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <xsl:text>= </xsl:text>
    <xsl:value-of select="$title"/>
    <xsl:text>&#10;</xsl:text>
    <xsl:call-template name="emit-authors">
      <xsl:with-param name="info" select="db:info"/>
    </xsl:call-template>
    <xsl:text>:doctype: book&#10;</xsl:text>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <!-- Emit author line(s) from info block -->
  <!-- In AsciiDoc, multiple authors must appear on a single line separated
       by "; ".  Emit them that way so the parser can recognise all authors. -->
  <xsl:template name="emit-authors">
    <xsl:param name="info"/>
    <xsl:variable name="authors" select="$info/db:author | $info/db:authorgroup/db:author"/>
    <xsl:for-each select="$authors">
      <xsl:variable name="fn" select="normalize-space(db:personname/db:firstname)"/>
      <xsl:variable name="on" select="normalize-space(db:personname/db:othername)"/>
      <xsl:variable name="sn" select="normalize-space(db:personname/db:surname)"/>
      <xsl:variable name="em" select="normalize-space(db:affiliation/db:address/db:email)"/>
      <xsl:variable name="fullname">
        <xsl:value-of select="$fn"/>
        <xsl:if test="$on != ''">
          <xsl:text> </xsl:text>
          <xsl:value-of select="$on"/>
        </xsl:if>
        <xsl:if test="$sn != ''">
          <xsl:text> </xsl:text>
          <xsl:value-of select="$sn"/>
        </xsl:if>
      </xsl:variable>
      <xsl:if test="normalize-space($fullname) != ''">
        <xsl:if test="position() &gt; 1">
          <xsl:text>; </xsl:text>
        </xsl:if>
        <xsl:value-of select="normalize-space($fullname)"/>
        <xsl:if test="$em != ''">
          <xsl:text> &lt;</xsl:text>
          <xsl:value-of select="$em"/>
          <xsl:text>&gt;</xsl:text>
        </xsl:if>
      </xsl:if>
    </xsl:for-each>
    <xsl:if test="$info/db:corpauthor">
      <xsl:if test="count($authors) &gt; 0">
        <xsl:text>; </xsl:text>
      </xsl:if>
      <xsl:value-of select="normalize-space($info/db:corpauthor)"/>
    </xsl:if>
    <!-- Final newline after all authors -->
    <xsl:if test="count($authors) &gt; 0 or $info/db:corpauthor">
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
  </xsl:template>

  <!-- ============================================================
       INFO BLOCK (skip rendering - handled in header templates)
       ============================================================ -->
  <xsl:template match="db:info[parent::db:article or parent::db:book]"/>
  <xsl:template match="db:refmeta"/>

  <!-- ============================================================
       MAN PAGE STRUCTURE
       ============================================================ -->

  <!-- NAME section -->
  <xsl:template match="db:refnamediv">
    <xsl:text>== NAME&#10;</xsl:text>
    <xsl:for-each select="db:refname">
      <xsl:value-of select="normalize-space(.)"/>
      <xsl:if test="position() != last()">
        <xsl:text>, </xsl:text>
      </xsl:if>
    </xsl:for-each>
    <xsl:if test="db:refpurpose">
      <xsl:text> - </xsl:text>
      <xsl:value-of select="normalize-space(db:refpurpose)"/>
    </xsl:if>
    <xsl:text>&#10;&#10;</xsl:text>
  </xsl:template>

  <!-- SYNOPSIS section -->
  <xsl:template match="db:refsynopsisdiv">
    <xsl:text>== SYNOPSIS&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <!-- Command synopsis block -->
  <xsl:template match="db:cmdsynopsis">
    <xsl:text>&#10;[source]&#10;</xsl:text>
    <xsl:text>----&#10;</xsl:text>
    <xsl:apply-templates mode="synopsis"/>
    <xsl:text>&#10;----&#10;&#10;</xsl:text>
  </xsl:template>

  <!-- Synopsis mode: render command synopsis elements as plain text -->
  <xsl:template match="db:command" mode="synopsis">
    <xsl:value-of select="normalize-space(.)"/>
  </xsl:template>

  <xsl:template match="db:arg" mode="synopsis">
    <xsl:text> </xsl:text>
    <xsl:variable name="choice" select="@choice"/>
    <xsl:variable name="rep" select="@rep"/>
    <xsl:choose>
      <xsl:when test="$choice = 'req'">
        <xsl:text>{</xsl:text>
        <xsl:apply-templates mode="synopsis"/>
        <xsl:text>}</xsl:text>
      </xsl:when>
      <xsl:when test="$choice = 'plain'">
        <xsl:apply-templates mode="synopsis"/>
      </xsl:when>
      <xsl:otherwise>
        <!-- opt is default -->
        <xsl:text>[</xsl:text>
        <xsl:apply-templates mode="synopsis"/>
        <xsl:text>]</xsl:text>
      </xsl:otherwise>
    </xsl:choose>
    <xsl:if test="$rep = 'repeat'">
      <xsl:text>...</xsl:text>
    </xsl:if>
  </xsl:template>

  <xsl:template match="db:replaceable" mode="synopsis">
    <xsl:text>&lt;</xsl:text>
    <xsl:value-of select="normalize-space(.)"/>
    <xsl:text>&gt;</xsl:text>
  </xsl:template>

  <xsl:template match="db:option" mode="synopsis">
    <xsl:value-of select="normalize-space(.)"/>
  </xsl:template>

  <xsl:template match="db:group" mode="synopsis">
    <xsl:text> </xsl:text>
    <xsl:variable name="choice" select="@choice"/>
    <xsl:variable name="rep" select="@rep"/>
    <xsl:choose>
      <xsl:when test="$choice = 'req'">
        <xsl:text>{</xsl:text>
        <xsl:for-each select="db:arg | db:group">
          <xsl:if test="position() > 1"><xsl:text>|</xsl:text></xsl:if>
          <xsl:apply-templates select="." mode="synopsis"/>
        </xsl:for-each>
        <xsl:text>}</xsl:text>
      </xsl:when>
      <xsl:otherwise>
        <xsl:text>[</xsl:text>
        <xsl:for-each select="db:arg | db:group">
          <xsl:if test="position() > 1"><xsl:text>|</xsl:text></xsl:if>
          <xsl:apply-templates select="." mode="synopsis"/>
        </xsl:for-each>
        <xsl:text>]</xsl:text>
      </xsl:otherwise>
    </xsl:choose>
    <xsl:if test="$rep = 'repeat'">
      <xsl:text>...</xsl:text>
    </xsl:if>
  </xsl:template>

  <xsl:template match="db:sbr" mode="synopsis">
    <xsl:text> \&#10;    </xsl:text>
  </xsl:template>

  <xsl:template match="db:synopfragment" mode="synopsis">
    <xsl:text>&#10;</xsl:text>
    <xsl:apply-templates mode="synopsis"/>
  </xsl:template>

  <xsl:template match="db:synopfragmentref" mode="synopsis">
    <xsl:text>&lt;</xsl:text>
    <xsl:value-of select="normalize-space(.)"/>
    <xsl:text>&gt;</xsl:text>
  </xsl:template>

  <!-- text in synopsis mode -->
  <xsl:template match="text()" mode="synopsis">
    <xsl:value-of select="normalize-space(.)"/>
  </xsl:template>

  <!-- Function synopsis -->
  <xsl:template match="db:funcsynopsis">
    <xsl:text>&#10;[source,c]&#10;</xsl:text>
    <xsl:text>----&#10;</xsl:text>
    <xsl:apply-templates mode="funcsynopsis"/>
    <xsl:text>&#10;----&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:funcprototype" mode="funcsynopsis">
    <xsl:apply-templates mode="funcsynopsis"/>
    <xsl:text>;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:funcdef" mode="funcsynopsis">
    <xsl:apply-templates mode="funcsynopsis"/>
  </xsl:template>

  <xsl:template match="db:function" mode="funcsynopsis">
    <xsl:value-of select="normalize-space(.)"/>
  </xsl:template>

  <xsl:template match="db:paramdef" mode="funcsynopsis">
    <xsl:if test="position() = 1"><xsl:text>(</xsl:text></xsl:if>
    <xsl:if test="position() > 1"><xsl:text>, </xsl:text></xsl:if>
    <xsl:apply-templates mode="funcsynopsis"/>
    <xsl:if test="position() = last()"><xsl:text>)</xsl:text></xsl:if>
  </xsl:template>

  <xsl:template match="db:void" mode="funcsynopsis">
    <xsl:text>(void)</xsl:text>
  </xsl:template>

  <xsl:template match="db:parameter" mode="funcsynopsis">
    <xsl:value-of select="normalize-space(.)"/>
  </xsl:template>

  <xsl:template match="text()" mode="funcsynopsis">
    <xsl:value-of select="normalize-space(.)"/>
  </xsl:template>

  <!-- refsect1/refsection: top-level man page sections (NAME, SYNOPSIS, etc.) -->
  <xsl:template match="db:refsection | db:refsect1">
    <xsl:variable name="level">
      <xsl:call-template name="heading-level"/>
    </xsl:variable>
    <xsl:if test="@xml:id">
      <xsl:text>[[</xsl:text>
      <xsl:value-of select="@xml:id"/>
      <xsl:text>]]&#10;</xsl:text>
    </xsl:if>
    <xsl:call-template name="section-mark">
      <xsl:with-param name="level" select="$level"/>
    </xsl:call-template>
    <xsl:text> </xsl:text>
    <xsl:value-of select="normalize-space(db:title | db:info/db:title)"/>
    <xsl:text>&#10;&#10;</xsl:text>
    <xsl:apply-templates select="*[not(self::db:title) and not(self::db:info)]"/>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:refsect2">
    <xsl:variable name="level">
      <xsl:call-template name="heading-level"/>
    </xsl:variable>
    <xsl:if test="@xml:id">
      <xsl:text>[[</xsl:text>
      <xsl:value-of select="@xml:id"/>
      <xsl:text>]]&#10;</xsl:text>
    </xsl:if>
    <xsl:call-template name="section-mark">
      <xsl:with-param name="level" select="$level"/>
    </xsl:call-template>
    <xsl:text> </xsl:text>
    <xsl:value-of select="normalize-space(db:title | db:info/db:title)"/>
    <xsl:text>&#10;&#10;</xsl:text>
    <xsl:apply-templates select="*[not(self::db:title) and not(self::db:info)]"/>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <!-- ============================================================
       STRUCTURAL SECTIONS (article / book)
       ============================================================ -->

  <xsl:template match="db:part">
    <xsl:variable name="level">
      <xsl:call-template name="heading-level"/>
    </xsl:variable>
    <xsl:if test="@xml:id">
      <xsl:text>[[</xsl:text>
      <xsl:value-of select="@xml:id"/>
      <xsl:text>]]&#10;</xsl:text>
    </xsl:if>
    <xsl:call-template name="section-mark">
      <xsl:with-param name="level" select="$level"/>
    </xsl:call-template>
    <xsl:text> </xsl:text>
    <xsl:value-of select="normalize-space(db:title | db:info/db:title)"/>
    <xsl:text>&#10;&#10;</xsl:text>
    <xsl:apply-templates select="*[not(self::db:title) and not(self::db:info)]"/>
  </xsl:template>

  <xsl:template match="db:chapter">
    <xsl:variable name="level">
      <xsl:call-template name="heading-level"/>
    </xsl:variable>
    <xsl:if test="@xml:id">
      <xsl:text>[[</xsl:text>
      <xsl:value-of select="@xml:id"/>
      <xsl:text>]]&#10;</xsl:text>
    </xsl:if>
    <xsl:call-template name="section-mark">
      <xsl:with-param name="level" select="$level"/>
    </xsl:call-template>
    <xsl:text> </xsl:text>
    <xsl:value-of select="normalize-space(db:title | db:info/db:title)"/>
    <xsl:text>&#10;&#10;</xsl:text>
    <xsl:apply-templates select="*[not(self::db:title) and not(self::db:info)]"/>
  </xsl:template>

  <xsl:template match="db:preface">
    <xsl:variable name="level">
      <xsl:call-template name="heading-level"/>
    </xsl:variable>
    <xsl:if test="@xml:id">
      <xsl:text>[[</xsl:text>
      <xsl:value-of select="@xml:id"/>
      <xsl:text>]]&#10;</xsl:text>
    </xsl:if>
    <xsl:text>[preface]&#10;</xsl:text>
    <xsl:call-template name="section-mark">
      <xsl:with-param name="level" select="$level"/>
    </xsl:call-template>
    <xsl:text> </xsl:text>
    <xsl:value-of select="normalize-space(db:title | db:info/db:title)"/>
    <xsl:text>&#10;&#10;</xsl:text>
    <xsl:apply-templates select="*[not(self::db:title) and not(self::db:info)]"/>
  </xsl:template>

  <xsl:template match="db:appendix">
    <xsl:variable name="level">
      <xsl:call-template name="heading-level"/>
    </xsl:variable>
    <xsl:if test="@xml:id">
      <xsl:text>[[</xsl:text>
      <xsl:value-of select="@xml:id"/>
      <xsl:text>]]&#10;</xsl:text>
    </xsl:if>
    <xsl:text>[appendix]&#10;</xsl:text>
    <xsl:call-template name="section-mark">
      <xsl:with-param name="level" select="$level"/>
    </xsl:call-template>
    <xsl:text> </xsl:text>
    <xsl:value-of select="normalize-space(db:title | db:info/db:title)"/>
    <xsl:text>&#10;&#10;</xsl:text>
    <xsl:apply-templates select="*[not(self::db:title) and not(self::db:info)]"/>
  </xsl:template>

  <xsl:template match="db:dedication">
    <xsl:variable name="level">
      <xsl:call-template name="heading-level"/>
    </xsl:variable>
    <xsl:if test="@xml:id">
      <xsl:text>[[</xsl:text>
      <xsl:value-of select="@xml:id"/>
      <xsl:text>]]&#10;</xsl:text>
    </xsl:if>
    <xsl:text>[dedication]&#10;</xsl:text>
    <xsl:call-template name="section-mark">
      <xsl:with-param name="level" select="$level"/>
    </xsl:call-template>
    <xsl:text> Dedication&#10;&#10;</xsl:text>
    <xsl:apply-templates/>
  </xsl:template>

  <xsl:template match="db:acknowledgements">
    <xsl:variable name="level">
      <xsl:call-template name="heading-level"/>
    </xsl:variable>
    <xsl:if test="@xml:id">
      <xsl:text>[[</xsl:text>
      <xsl:value-of select="@xml:id"/>
      <xsl:text>]]&#10;</xsl:text>
    </xsl:if>
    <xsl:call-template name="section-mark">
      <xsl:with-param name="level" select="$level"/>
    </xsl:call-template>
    <xsl:text> Acknowledgements&#10;&#10;</xsl:text>
    <xsl:apply-templates/>
  </xsl:template>

  <xsl:template match="db:section">
    <xsl:variable name="level">
      <xsl:call-template name="heading-level"/>
    </xsl:variable>
    <xsl:variable name="title-text">
      <xsl:value-of select="normalize-space(db:title | db:info/db:title)"/>
    </xsl:variable>
    <xsl:if test="@xml:id">
      <xsl:text>[[</xsl:text>
      <xsl:value-of select="@xml:id"/>
      <xsl:text>]]&#10;</xsl:text>
    </xsl:if>
    <!-- Only emit a heading if there is a title -->
    <xsl:if test="$title-text != ''">
      <xsl:call-template name="section-mark">
        <xsl:with-param name="level" select="$level"/>
      </xsl:call-template>
      <xsl:text> </xsl:text>
      <xsl:value-of select="$title-text"/>
      <xsl:text>&#10;&#10;</xsl:text>
    </xsl:if>
    <xsl:apply-templates select="*[not(self::db:title) and not(self::db:info)]"/>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <!-- bridgehead (informal heading) -->
  <xsl:template match="db:bridgehead">
    <xsl:variable name="renderas" select="@renderas"/>
    <xsl:text>&#10;[discrete]&#10;</xsl:text>
    <xsl:choose>
      <xsl:when test="$renderas = 'sect1'">==</xsl:when>
      <xsl:when test="$renderas = 'sect2'">===</xsl:when>
      <xsl:when test="$renderas = 'sect3'">====</xsl:when>
      <xsl:when test="$renderas = 'sect4'">=====</xsl:when>
      <xsl:otherwise>===</xsl:otherwise>
    </xsl:choose>
    <xsl:text> </xsl:text>
    <xsl:apply-templates/>
    <xsl:text>&#10;&#10;</xsl:text>
  </xsl:template>

  <!-- ============================================================
       PARAGRAPHS
       ============================================================ -->

  <xsl:template match="db:para | db:simpara">
    <xsl:apply-templates/>
    <xsl:text>&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:formalpara">
    <xsl:text>.</xsl:text>
    <xsl:value-of select="normalize-space(db:title)"/>
    <xsl:text>&#10;</xsl:text>
    <xsl:apply-templates select="db:para"/>
  </xsl:template>

  <!-- ============================================================
       ADMONITIONS
       ============================================================ -->

  <xsl:template match="db:note">
    <xsl:call-template name="block-sep"/>
    <xsl:text>&#10;[NOTE]&#10;====&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>====&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:warning">
    <xsl:call-template name="block-sep"/>
    <xsl:text>&#10;[WARNING]&#10;====&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>====&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:caution">
    <xsl:call-template name="block-sep"/>
    <xsl:text>&#10;[CAUTION]&#10;====&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>====&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:tip">
    <xsl:call-template name="block-sep"/>
    <xsl:text>&#10;[TIP]&#10;====&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>====&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:important">
    <xsl:call-template name="block-sep"/>
    <xsl:text>&#10;[IMPORTANT]&#10;====&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>====&#10;&#10;</xsl:text>
  </xsl:template>

  <!-- ============================================================
       BLOCK QUOTES
       ============================================================ -->

  <xsl:template match="db:blockquote">
    <xsl:call-template name="block-sep"/>
    <xsl:text>&#10;[quote</xsl:text>
    <xsl:if test="db:attribution">
      <xsl:text>, </xsl:text>
      <xsl:value-of select="normalize-space(db:attribution)"/>
    </xsl:if>
    <xsl:text>]&#10;____&#10;</xsl:text>
    <xsl:apply-templates select="*[not(self::db:attribution)]"/>
    <xsl:text>____&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:epigraph">
    <xsl:call-template name="block-sep"/>
    <xsl:text>&#10;[quote</xsl:text>
    <xsl:if test="db:attribution">
      <xsl:text>, </xsl:text>
      <xsl:value-of select="normalize-space(db:attribution)"/>
    </xsl:if>
    <xsl:text>]&#10;____&#10;</xsl:text>
    <xsl:apply-templates select="*[not(self::db:attribution)]"/>
    <xsl:text>____&#10;&#10;</xsl:text>
  </xsl:template>

  <!-- ============================================================
       CODE BLOCKS
       ============================================================ -->

  <xsl:template match="db:programlisting">
    <xsl:call-template name="block-sep"/>
    <xsl:variable name="lang">
      <xsl:choose>
        <xsl:when test="@language"><xsl:value-of select="@language"/></xsl:when>
        <xsl:otherwise></xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <xsl:text>&#10;</xsl:text>
    <xsl:if test="$lang != ''">
      <xsl:text>[source,</xsl:text>
      <xsl:value-of select="$lang"/>
      <xsl:text>]&#10;</xsl:text>
    </xsl:if>
    <xsl:text>----&#10;</xsl:text>
    <xsl:value-of select="."/>
    <xsl:if test="substring(., string-length(.), 1) != '&#10;'">
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
    <xsl:text>----&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:screen | db:computeroutput[parent::db:para/parent::db:example or parent::db:para/parent::db:refsection]">
    <xsl:call-template name="block-sep"/>
    <xsl:text>&#10;....&#10;</xsl:text>
    <xsl:value-of select="."/>
    <xsl:if test="substring(., string-length(.), 1) != '&#10;'">
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
    <xsl:text>....&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:literallayout">
    <xsl:call-template name="block-sep"/>
    <xsl:text>&#10;....&#10;</xsl:text>
    <xsl:value-of select="."/>
    <xsl:if test="substring(., string-length(.), 1) != '&#10;'">
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
    <xsl:text>....&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:synopsis">
    <xsl:call-template name="block-sep"/>
    <xsl:text>&#10;----&#10;</xsl:text>
    <xsl:value-of select="."/>
    <xsl:if test="substring(., string-length(.), 1) != '&#10;'">
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
    <xsl:text>----&#10;&#10;</xsl:text>
  </xsl:template>

  <!-- ============================================================
       EXAMPLES / FORMAL PARAGRAPHS
       ============================================================ -->

  <xsl:template match="db:example">
    <xsl:call-template name="block-sep"/>
    <xsl:if test="db:title">
      <xsl:text>.</xsl:text>
      <xsl:value-of select="normalize-space(db:title)"/>
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
    <xsl:text>[example]&#10;====&#10;</xsl:text>
    <xsl:apply-templates select="*[not(self::db:title)]"/>
    <xsl:text>====&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:informalexample">
    <xsl:call-template name="block-sep"/>
    <xsl:text>[example]&#10;====&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>====&#10;&#10;</xsl:text>
  </xsl:template>

  <!-- ============================================================
       LISTS
       ============================================================ -->

  <xsl:template match="db:itemizedlist">
    <xsl:param name="depth" select="0"/>
    <xsl:call-template name="block-sep"/>
    <xsl:text>&#10;</xsl:text>
    <xsl:apply-templates select="db:listitem">
      <xsl:with-param name="depth" select="$depth"/>
      <xsl:with-param name="type" select="'bullet'"/>
    </xsl:apply-templates>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:orderedlist">
    <xsl:param name="depth" select="0"/>
    <xsl:call-template name="block-sep"/>
    <xsl:text>&#10;</xsl:text>
    <xsl:apply-templates select="db:listitem">
      <xsl:with-param name="depth" select="$depth"/>
      <xsl:with-param name="type" select="'number'"/>
    </xsl:apply-templates>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:listitem">
    <xsl:param name="depth" select="0"/>
    <xsl:param name="type" select="'bullet'"/>
    <!-- Compute nesting based on ancestor list depth -->
    <xsl:variable name="nest-depth">
      <xsl:choose>
        <xsl:when test="$depth > 0">
          <xsl:value-of select="$depth"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="count(ancestor::db:itemizedlist) + count(ancestor::db:orderedlist) - 1"/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <xsl:choose>
      <xsl:when test="$type = 'bullet'">
        <xsl:call-template name="list-bullet">
          <xsl:with-param name="depth" select="$nest-depth"/>
        </xsl:call-template>
      </xsl:when>
      <xsl:otherwise>
        <xsl:call-template name="list-number">
          <xsl:with-param name="depth" select="$nest-depth"/>
        </xsl:call-template>
      </xsl:otherwise>
    </xsl:choose>
    <xsl:text> </xsl:text>
    <!-- First paragraph inline, rest as continuation blocks -->
    <xsl:for-each select="*">
      <xsl:choose>
        <xsl:when test="position() = 1 and (self::db:para or self::db:simpara)">
          <xsl:apply-templates/>
          <xsl:text>&#10;</xsl:text>
        </xsl:when>
        <xsl:when test="self::db:itemizedlist or self::db:orderedlist or self::db:variablelist">
          <xsl:apply-templates select="."/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:text>+&#10;</xsl:text>
          <xsl:apply-templates select="."/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:for-each>
  </xsl:template>

  <!-- simplelist -->
  <xsl:template match="db:simplelist">
    <xsl:call-template name="block-sep"/>
    <xsl:text>&#10;</xsl:text>
    <xsl:for-each select="db:member">
      <xsl:text>* </xsl:text>
      <xsl:apply-templates/>
      <xsl:text>&#10;</xsl:text>
    </xsl:for-each>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <!-- ============================================================
       VARIABLE / DEFINITION LISTS
       ============================================================ -->

  <xsl:template match="db:variablelist">
    <xsl:call-template name="block-sep"/>
    <xsl:text>&#10;</xsl:text>
    <xsl:apply-templates select="db:varlistentry"/>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:varlistentry">
    <!-- term(s) -->
    <xsl:for-each select="db:term">
      <xsl:apply-templates/>
      <xsl:if test="position() != last()">
        <xsl:text>&#10;</xsl:text>
      </xsl:if>
    </xsl:for-each>
    <xsl:text>::&#10;</xsl:text>
    <!-- definition content -->
    <xsl:apply-templates select="db:listitem/*"/>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <!-- ============================================================
       TABLES
       ============================================================ -->

  <!-- Compute the number of columns an entry spans.
       Checks @namest/@nameend directly, then falls back to @spanname.
       Returns 1 if the entry does not span multiple columns. -->
  <xsl:template name="entry-colspan">
    <xsl:param name="entry" select="."/>
    <xsl:variable name="namest">
      <xsl:choose>
        <xsl:when test="$entry/@namest">
          <xsl:value-of select="$entry/@namest"/>
        </xsl:when>
        <xsl:when test="$entry/@spanname">
          <xsl:variable name="sn" select="$entry/@spanname"/>
          <xsl:value-of select="ancestor::db:tgroup/db:spanspec[@spanname=$sn]/@namest"/>
        </xsl:when>
      </xsl:choose>
    </xsl:variable>
    <xsl:variable name="nameend">
      <xsl:choose>
        <xsl:when test="$entry/@nameend">
          <xsl:value-of select="$entry/@nameend"/>
        </xsl:when>
        <xsl:when test="$entry/@spanname">
          <xsl:variable name="sn" select="$entry/@spanname"/>
          <xsl:value-of select="ancestor::db:tgroup/db:spanspec[@spanname=$sn]/@nameend"/>
        </xsl:when>
      </xsl:choose>
    </xsl:variable>
    <xsl:choose>
      <xsl:when test="$namest != '' and $nameend != '' and $namest != $nameend">
        <!-- Count columns from namest to nameend inclusive -->
        <xsl:variable name="pos-start"
          select="count(ancestor::db:tgroup/db:colspec[@colname=$namest][1]/preceding-sibling::db:colspec) + 1"/>
        <xsl:variable name="pos-end"
          select="count(ancestor::db:tgroup/db:colspec[@colname=$nameend][1]/preceding-sibling::db:colspec) + 1"/>
        <xsl:choose>
          <xsl:when test="$pos-end >= $pos-start">
            <xsl:value-of select="$pos-end - $pos-start + 1"/>
          </xsl:when>
          <xsl:otherwise>1</xsl:otherwise>
        </xsl:choose>
      </xsl:when>
      <xsl:otherwise>1</xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="db:table | db:informaltable">
    <xsl:call-template name="block-sep"/>
    <xsl:if test="@xml:id">
      <xsl:text>[[</xsl:text>
      <xsl:value-of select="@xml:id"/>
      <xsl:text>]]&#10;</xsl:text>
    </xsl:if>
    <xsl:if test="db:title or db:info/db:title">
      <xsl:text>.</xsl:text>
      <xsl:value-of select="normalize-space(db:title | db:info/db:title)"/>
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
    <!-- Emit a [cols="N*"] attribute when the DocBook tgroup declares the
         column count.  Using the repeat notation "N*" tells the AsciiDoc
         parser how many columns to expect; this is critical for tables
         where each entry is emitted on its own line (multi-line cells)
         so the parser does not prematurely commit a one-cell row. -->
    <xsl:if test=".//db:tgroup[1]/@cols">
      <xsl:text>[cols="</xsl:text>
      <xsl:value-of select=".//db:tgroup[1]/@cols"/>
      <xsl:text>*"]&#10;</xsl:text>
    </xsl:if>
    <!-- Add %noheader option when there is no explicit thead block so
         AsciiDoc does not promote the first body row to a header row. -->
    <xsl:if test="not(.//db:thead)">
      <xsl:text>[%noheader]&#10;</xsl:text>
    </xsl:if>
    <xsl:text>|===&#10;</xsl:text>
    <!-- Process thead rows first, followed by a blank line to mark the
         header/body boundary in AsciiDoc table syntax. -->
    <xsl:if test=".//db:thead">
      <xsl:apply-templates select=".//db:thead/db:row"/>
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
    <!-- Process tbody rows -->
    <xsl:apply-templates select=".//db:tbody/db:row"/>
    <!-- Process tfoot rows -->
    <xsl:apply-templates select=".//db:tfoot/db:row"/>
    <xsl:text>|===&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:row">
    <!-- Emit each entry on its own line so that multi-line cell content
         (e.g. a paragraph followed by an image) is correctly accumulated
         by the AsciiDoc parser into a single cell.  The first entry of
         each row immediately follows the previous row's trailing newline
         (no leading blank line) to avoid creating a spurious header/body
         separator before the first row of a table. -->
    <xsl:for-each select="db:entry">
      <!-- Compute column-span and row-span prefixes. -->
      <xsl:variable name="colspan">
        <xsl:call-template name="entry-colspan"/>
      </xsl:variable>
      <xsl:variable name="rowspan">
        <xsl:choose>
          <xsl:when test="@morerows and @morerows > 0">
            <xsl:value-of select="@morerows + 1"/>
          </xsl:when>
          <xsl:otherwise>0</xsl:otherwise>
        </xsl:choose>
      </xsl:variable>
      <!-- Entries after the first within a row each start on their own line.
           The first entry follows directly on the same line as the row
           context (the trailing newline of the previous row or |===). -->
      <xsl:if test="position() > 1">
        <xsl:text>&#10;</xsl:text>
      </xsl:if>
      <!-- Emit row-span prefix .N+ when the cell spans multiple rows. -->
      <xsl:if test="$rowspan > 1">
        <xsl:text>.</xsl:text>
        <xsl:value-of select="$rowspan"/>
        <xsl:text>+</xsl:text>
      </xsl:if>
      <!-- Emit column-span prefix N+ when the cell spans multiple columns. -->
      <xsl:if test="$colspan > 1">
        <xsl:value-of select="$colspan"/>
        <xsl:text>+</xsl:text>
      </xsl:if>
      <xsl:text>|</xsl:text>
      <xsl:apply-templates mode="table-cell"/>
    </xsl:for-each>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <!-- ============================================================
       TABLE CELL MODE
       Inside a table cell we suppress block-level newlines so that
       each entry's content stays on a single (possibly long) line.
       Block elements are separated by a space instead.
       ============================================================ -->

  <!-- Paragraphs inside cells: inline, separated by a single space. -->
  <xsl:template match="db:para | db:simpara" mode="table-cell">
    <xsl:apply-templates mode="table-cell"/>
    <xsl:text> </xsl:text>
  </xsl:template>

  <!-- Emphasis/bold in cell mode -->
  <xsl:template match="db:emphasis" mode="table-cell">
    <xsl:choose>
      <xsl:when test="@role = 'bold' or @role = 'strong'">
        <xsl:text>*</xsl:text>
        <xsl:apply-templates mode="table-cell"/>
        <xsl:text>*</xsl:text>
      </xsl:when>
      <xsl:when test="@role = 'underline'">
        <xsl:text>[.underline]#</xsl:text>
        <xsl:apply-templates mode="table-cell"/>
        <xsl:text>#</xsl:text>
      </xsl:when>
      <xsl:otherwise>
        <xsl:text>_</xsl:text>
        <xsl:apply-templates mode="table-cell"/>
        <xsl:text>_</xsl:text>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="db:command | db:option | db:userinput" mode="table-cell">
    <xsl:text>*</xsl:text>
    <xsl:apply-templates mode="table-cell"/>
    <xsl:text>*</xsl:text>
  </xsl:template>

  <xsl:template match="db:literal | db:code | db:constant | db:type | db:markup
                       | db:filename | db:varname | db:envar | db:systemitem
                       | db:computeroutput" mode="table-cell">
    <xsl:text>`</xsl:text>
    <xsl:apply-templates mode="table-cell"/>
    <xsl:text>`</xsl:text>
  </xsl:template>

  <xsl:template match="db:replaceable | db:parameter | db:application" mode="table-cell">
    <xsl:text>_</xsl:text>
    <xsl:apply-templates mode="table-cell"/>
    <xsl:text>_</xsl:text>
  </xsl:template>

  <xsl:template match="db:function" mode="table-cell">
    <xsl:text>`</xsl:text>
    <xsl:apply-templates mode="table-cell"/>
    <xsl:text>()`</xsl:text>
  </xsl:template>

  <xsl:template match="db:superscript" mode="table-cell">
    <xsl:text>^</xsl:text>
    <xsl:apply-templates mode="table-cell"/>
    <xsl:text>^</xsl:text>
  </xsl:template>

  <xsl:template match="db:subscript" mode="table-cell">
    <xsl:text>~</xsl:text>
    <xsl:apply-templates mode="table-cell"/>
    <xsl:text>~</xsl:text>
  </xsl:template>

  <xsl:template match="db:quote" mode="table-cell">
    <xsl:text>&#8220;</xsl:text>
    <xsl:apply-templates mode="table-cell"/>
    <xsl:text>&#8221;</xsl:text>
  </xsl:template>

  <xsl:template match="db:link" mode="table-cell">
    <xsl:choose>
      <xsl:when test="@xl:href">
        <xsl:value-of select="@xl:href"/>
        <xsl:if test="normalize-space(.) != ''">
          <xsl:text>[</xsl:text>
          <xsl:apply-templates mode="table-cell"/>
          <xsl:text>]</xsl:text>
        </xsl:if>
      </xsl:when>
      <xsl:when test="@linkend">
        <xsl:text>&lt;&lt;</xsl:text>
        <xsl:value-of select="@linkend"/>
        <xsl:if test="normalize-space(.) != ''">
          <xsl:text>,</xsl:text>
          <xsl:apply-templates mode="table-cell"/>
        </xsl:if>
        <xsl:text>&gt;&gt;</xsl:text>
      </xsl:when>
      <xsl:otherwise>
        <xsl:apply-templates mode="table-cell"/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="db:xref" mode="table-cell">
    <xsl:text>&lt;&lt;</xsl:text>
    <xsl:value-of select="@linkend"/>
    <xsl:text>&gt;&gt;</xsl:text>
  </xsl:template>

  <xsl:template match="db:citerefentry" mode="table-cell">
    <xsl:text>*</xsl:text>
    <xsl:value-of select="normalize-space(db:refentrytitle)"/>
    <xsl:text>*(</xsl:text>
    <xsl:value-of select="normalize-space(db:manvolnum)"/>
    <xsl:text>)</xsl:text>
  </xsl:template>

  <!-- Inline images inside cells use the inline image: macro. -->
  <xsl:template match="db:inlinemediaobject" mode="table-cell">
    <xsl:call-template name="preferred-imagedata-inline"/>
  </xsl:template>

  <!-- Block images inside cells are rendered inline (image: not image::). -->
  <xsl:template match="db:mediaobject" mode="table-cell">
    <xsl:call-template name="preferred-imagedata-inline"/>
  </xsl:template>

  <!-- Nested tables inside cells: render as a flat list of values
       separated by commas, since AsciiDoc nested table syntax (!=== )
       is not yet supported by asciiquack. -->
  <xsl:template match="db:table | db:informaltable" mode="table-cell">
    <xsl:for-each select=".//db:entry">
      <xsl:if test="position() > 1">
        <xsl:text>, </xsl:text>
      </xsl:if>
      <xsl:apply-templates mode="table-cell"/>
    </xsl:for-each>
  </xsl:template>

  <!-- Programlisting / screen inside cells: inline monospace. -->
  <xsl:template match="db:programlisting | db:screen | db:literallayout
                       | db:synopsis" mode="table-cell">
    <xsl:text>`</xsl:text>
    <xsl:value-of select="normalize-space(.)"/>
    <xsl:text>`</xsl:text>
  </xsl:template>

  <!-- Admonitions in cells: just render the body text. -->
  <xsl:template match="db:note | db:warning | db:caution | db:tip
                       | db:important" mode="table-cell">
    <xsl:apply-templates mode="table-cell"/>
  </xsl:template>

  <!-- Lists inside cells: comma-separated inline form. -->
  <xsl:template match="db:itemizedlist | db:orderedlist" mode="table-cell">
    <xsl:for-each select="db:listitem">
      <xsl:if test="position() > 1">
        <xsl:text>; </xsl:text>
      </xsl:if>
      <xsl:apply-templates mode="table-cell"/>
    </xsl:for-each>
  </xsl:template>

  <xsl:template match="db:listitem" mode="table-cell">
    <xsl:apply-templates mode="table-cell"/>
  </xsl:template>

  <!-- footnotes inside cells -->
  <xsl:template match="db:footnote" mode="table-cell">
    <xsl:text>footnote:[</xsl:text>
    <xsl:apply-templates mode="table-cell"/>
    <xsl:text>]</xsl:text>
  </xsl:template>

  <!-- Generic inline passthrough for table-cell mode: delegate to
       apply-templates so any unmatched element still processes children. -->
  <xsl:template match="db:acronym | db:abbrev | db:prompt
                       | db:uri | db:email" mode="table-cell">
    <xsl:apply-templates mode="table-cell"/>
  </xsl:template>

  <!-- text() in table-cell mode: normalize whitespace (same as default). -->
  <xsl:template match="text()" mode="table-cell">
    <xsl:if test="preceding-sibling::node() and
                  string-length(normalize-space(.)) > 0 and
                  translate(substring(.,1,1),' &#9;&#10;&#13;','') = ''">
      <xsl:text> </xsl:text>
    </xsl:if>
    <xsl:value-of select="normalize-space(.)"/>
    <xsl:if test="following-sibling::node() and
                  string-length(.) > 1 and
                  string-length(normalize-space(.)) > 0 and
                  translate(substring(.,string-length(.),1),' &#9;&#10;&#13;','') = ''">
      <xsl:text> </xsl:text>
    </xsl:if>
  </xsl:template>

  <!-- Default catchall in table-cell mode: process children. -->
  <xsl:template match="*" mode="table-cell">
    <xsl:apply-templates mode="table-cell"/>
  </xsl:template>

  <!-- ============================================================
       IMAGES / FIGURES
       ============================================================ -->

  <!-- Select the single preferred imagedata from a mediaobject context:
       prefer role='html', then no role, then any first imageobject. -->
  <xsl:template name="preferred-imagedata">
    <xsl:choose>
      <xsl:when test="db:imageobject[@role='html']/db:imagedata">
        <xsl:apply-templates select="db:imageobject[@role='html'][1]/db:imagedata"/>
      </xsl:when>
      <xsl:when test="db:imageobject[not(@role)]/db:imagedata">
        <xsl:apply-templates select="db:imageobject[not(@role)][1]/db:imagedata"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:apply-templates select="db:imageobject[1]/db:imagedata"/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <!-- Same selection but in inline (image:) mode -->
  <xsl:template name="preferred-imagedata-inline">
    <xsl:choose>
      <xsl:when test="db:imageobject[@role='html']/db:imagedata">
        <xsl:apply-templates select="db:imageobject[@role='html'][1]/db:imagedata" mode="inline"/>
      </xsl:when>
      <xsl:when test="db:imageobject[not(@role)]/db:imagedata">
        <xsl:apply-templates select="db:imageobject[not(@role)][1]/db:imagedata" mode="inline"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:apply-templates select="db:imageobject[1]/db:imagedata" mode="inline"/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="db:figure | db:informalfigure">
    <xsl:call-template name="block-sep"/>
    <xsl:if test="@xml:id">
      <xsl:text>[[</xsl:text>
      <xsl:value-of select="@xml:id"/>
      <xsl:text>]]&#10;</xsl:text>
    </xsl:if>
    <!-- DocBook 5 allows the title inside an <info> child; check both. -->
    <xsl:if test="db:title or db:info/db:title">
      <xsl:text>.</xsl:text>
      <xsl:value-of select="normalize-space(db:title | db:info/db:title)"/>
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
    <xsl:for-each select=".//db:mediaobject[1]">
      <xsl:call-template name="preferred-imagedata"/>
    </xsl:for-each>
  </xsl:template>

  <xsl:template match="db:mediaobject">
    <xsl:call-template name="block-sep"/>
    <!-- In AsciiDoc the block title (caption) must appear BEFORE the image
         macro.  Emit it first, then the image. -->
    <xsl:if test="db:caption">
      <xsl:text>.</xsl:text>
      <xsl:apply-templates select="db:caption/*"/>
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
    <xsl:call-template name="preferred-imagedata"/>
  </xsl:template>

  <xsl:template match="db:inlinemediaobject">
    <xsl:call-template name="preferred-imagedata-inline"/>
  </xsl:template>

  <!-- Extract alt text: prefer @alt attribute, then textobject/phrase,
       then textobject/para. -->
  <xsl:template name="imagedata-alt">
    <!-- Walk up to the containing mediaobject to find the textobject. -->
    <xsl:variable name="mo" select="ancestor::db:mediaobject[1] |
                                     ancestor::db:inlinemediaobject[1]"/>
    <xsl:choose>
      <xsl:when test="@alt">
        <xsl:value-of select="@alt"/>
      </xsl:when>
      <xsl:when test="$mo/db:textobject/db:phrase">
        <xsl:value-of select="normalize-space($mo/db:textobject/db:phrase)"/>
      </xsl:when>
      <xsl:when test="$mo/db:textobject/db:para">
        <xsl:value-of select="normalize-space($mo/db:textobject/db:para)"/>
      </xsl:when>
    </xsl:choose>
  </xsl:template>

  <!-- Block image macro (image::) -->
  <xsl:template match="db:imagedata">
    <xsl:variable name="alt">
      <xsl:call-template name="imagedata-alt"/>
    </xsl:variable>
    <xsl:text>image::</xsl:text>
    <xsl:value-of select="@fileref"/>
    <xsl:text>[</xsl:text>
    <xsl:value-of select="$alt"/>
    <xsl:if test="@width">
      <xsl:if test="$alt != ''">
        <xsl:text>,</xsl:text>
      </xsl:if>
      <xsl:text>width=</xsl:text>
      <xsl:value-of select="@width"/>
    </xsl:if>
    <xsl:text>]&#10;&#10;</xsl:text>
  </xsl:template>

  <!-- Inline image macro (image:) - no trailing blank line -->
  <xsl:template match="db:imagedata" mode="inline">
    <xsl:variable name="alt">
      <xsl:call-template name="imagedata-alt"/>
    </xsl:variable>
    <xsl:text>image:</xsl:text>
    <xsl:value-of select="@fileref"/>
    <xsl:text>[</xsl:text>
    <xsl:value-of select="$alt"/>
    <xsl:if test="@width">
      <xsl:if test="$alt != ''">
        <xsl:text>,</xsl:text>
      </xsl:if>
      <xsl:text>width=</xsl:text>
      <xsl:value-of select="@width"/>
    </xsl:if>
    <xsl:text>]</xsl:text>
  </xsl:template>

  <!-- imageobject is handled via preferred-imagedata* named templates;
       suppress default processing.  textobject is consumed via the
       imagedata-alt named template above; suppress it here so it does not
       produce stray text output. -->
  <xsl:template match="db:imageobject | db:textobject"/>

  <xsl:template match="db:screenshot">
    <xsl:apply-templates/>
  </xsl:template>

  <!-- ============================================================
       INLINE MARKUP
       ============================================================ -->

  <!-- emphasis: italic by default, bold if role="bold" -->
  <xsl:template match="db:emphasis">
    <xsl:choose>
      <xsl:when test="@role = 'bold' or @role = 'strong'">
        <xsl:text>*</xsl:text>
        <xsl:apply-templates/>
        <xsl:text>*</xsl:text>
      </xsl:when>
      <xsl:when test="@role = 'underline'">
        <xsl:text>[.underline]#</xsl:text>
        <xsl:apply-templates/>
        <xsl:text>#</xsl:text>
      </xsl:when>
      <xsl:otherwise>
        <xsl:text>_</xsl:text>
        <xsl:apply-templates/>
        <xsl:text>_</xsl:text>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <!-- command: bold monospace -->
  <xsl:template match="db:command">
    <xsl:text>*</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>*</xsl:text>
  </xsl:template>

  <!-- option: bold -->
  <xsl:template match="db:option">
    <xsl:text>*</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>*</xsl:text>
  </xsl:template>

  <!-- replaceable: italic -->
  <xsl:template match="db:replaceable">
    <xsl:text>_</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>_</xsl:text>
  </xsl:template>

  <!-- literal, code: monospace -->
  <xsl:template match="db:literal | db:code | db:constant | db:type | db:markup">
    <xsl:text>`</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>`</xsl:text>
  </xsl:template>

  <!-- filename, varname, envar: monospace italic -->
  <xsl:template match="db:filename | db:varname | db:envar | db:systemitem">
    <xsl:text>`</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>`</xsl:text>
  </xsl:template>

  <!-- application: italic -->
  <xsl:template match="db:application">
    <xsl:text>_</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>_</xsl:text>
  </xsl:template>

  <!-- function: monospace -->
  <xsl:template match="db:function">
    <xsl:text>`</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>()`</xsl:text>
  </xsl:template>

  <!-- parameter: italic monospace -->
  <xsl:template match="db:parameter">
    <xsl:text>_</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>_</xsl:text>
  </xsl:template>

  <!-- userinput: bold -->
  <xsl:template match="db:userinput">
    <xsl:text>*</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>*</xsl:text>
  </xsl:template>

  <!-- computeroutput: monospace -->
  <xsl:template match="db:computeroutput">
    <xsl:text>`</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>`</xsl:text>
  </xsl:template>

  <!-- prompt: just output text -->
  <xsl:template match="db:prompt">
    <xsl:apply-templates/>
  </xsl:template>

  <!-- quote -->
  <xsl:template match="db:quote">
    <xsl:text>&#8220;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>&#8221;</xsl:text>
  </xsl:template>

  <!-- acronym, abbrev -->
  <xsl:template match="db:acronym | db:abbrev">
    <xsl:apply-templates/>
  </xsl:template>

  <!-- superscript, subscript -->
  <xsl:template match="db:superscript">
    <xsl:text>^</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>^</xsl:text>
  </xsl:template>

  <xsl:template match="db:subscript">
    <xsl:text>~</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>~</xsl:text>
  </xsl:template>

  <!-- ============================================================
       CROSS-REFERENCES AND LINKS
       ============================================================ -->

  <!-- citerefentry: link to another man page -->
  <xsl:template match="db:citerefentry">
    <xsl:text>*</xsl:text>
    <xsl:value-of select="normalize-space(db:refentrytitle)"/>
    <xsl:text>*(</xsl:text>
    <xsl:value-of select="normalize-space(db:manvolnum)"/>
    <xsl:text>)</xsl:text>
  </xsl:template>

  <!-- link -->
  <xsl:template match="db:link">
    <xsl:choose>
      <xsl:when test="@xl:href">
        <xsl:value-of select="@xl:href"/>
        <xsl:if test="normalize-space(.) != ''">
          <xsl:text>[</xsl:text>
          <xsl:apply-templates/>
          <xsl:text>]</xsl:text>
        </xsl:if>
      </xsl:when>
      <xsl:when test="@linkend">
        <xsl:text>&lt;&lt;</xsl:text>
        <xsl:value-of select="@linkend"/>
        <xsl:if test="normalize-space(.) != ''">
          <xsl:text>,</xsl:text>
          <xsl:apply-templates/>
        </xsl:if>
        <xsl:text>&gt;&gt;</xsl:text>
      </xsl:when>
      <xsl:otherwise>
        <xsl:apply-templates/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <!-- ulink (DocBook 4 compat) -->
  <xsl:template match="db:ulink">
    <xsl:value-of select="@url"/>
    <xsl:if test="normalize-space(.) != ''">
      <xsl:text>[</xsl:text>
      <xsl:apply-templates/>
      <xsl:text>]</xsl:text>
    </xsl:if>
  </xsl:template>

  <!-- xref -->
  <xsl:template match="db:xref">
    <xsl:text>&lt;&lt;</xsl:text>
    <xsl:value-of select="@linkend"/>
    <xsl:text>&gt;&gt;</xsl:text>
  </xsl:template>

  <!-- uri -->
  <xsl:template match="db:uri">
    <xsl:apply-templates/>
  </xsl:template>

  <!-- email -->
  <xsl:template match="db:email">
    <xsl:text>&lt;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>&gt;</xsl:text>
  </xsl:template>

  <!-- footnote -->
  <xsl:template match="db:footnote">
    <xsl:text>footnote:[</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>]</xsl:text>
  </xsl:template>

  <xsl:template match="db:footnoteref">
    <xsl:text>footnote:[</xsl:text>
    <xsl:value-of select="@linkend"/>
    <xsl:text>]</xsl:text>
  </xsl:template>

  <!-- ============================================================
       MATHEMATICAL CONTENT
       ============================================================ -->

  <xsl:template match="db:mathphrase | db:inlineequation | db:informalequation">
    <xsl:text>stem:[</xsl:text>
    <xsl:value-of select="normalize-space(.)"/>
    <xsl:text>]</xsl:text>
  </xsl:template>

  <!-- ============================================================
       PROCEDURES (steps)
       ============================================================ -->

  <xsl:template match="db:procedure">
    <xsl:if test="db:title">
      <xsl:text>.</xsl:text>
      <xsl:value-of select="normalize-space(db:title)"/>
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
    <xsl:apply-templates select="db:step | db:substeps"/>
  </xsl:template>

  <xsl:template match="db:step">
    <xsl:text>. </xsl:text>
    <xsl:apply-templates/>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:substeps">
    <xsl:apply-templates select="db:step"/>
  </xsl:template>

  <!-- ============================================================
       GLOSSARY
       ============================================================ -->

  <xsl:template match="db:glossterm">
    <xsl:apply-templates/>
    <xsl:text>::&#10;</xsl:text>
  </xsl:template>

  <!-- ============================================================
       BIBLIOGRAPHY
       ============================================================ -->

  <xsl:template match="db:bibliography">
    <xsl:text>== Bibliography&#10;&#10;</xsl:text>
    <xsl:apply-templates/>
  </xsl:template>

  <xsl:template match="db:biblioentry">
    <xsl:text>* </xsl:text>
    <xsl:apply-templates/>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:biblioid">
    <xsl:apply-templates/>
  </xsl:template>

  <xsl:template match="db:citetitle">
    <xsl:text>_</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>_</xsl:text>
  </xsl:template>

  <!-- ============================================================
       XI:INCLUDE / XInclude
       ============================================================ -->

  <!-- xi:include should be processed by xsltproc -xinclude, so elements
       that remain are either errors or already expanded. Ignore xi: namespace. -->
  <xsl:template match="*[namespace-uri()='http://www.w3.org/2001/XInclude']"/>

  <!-- ============================================================
       SKIPPED ELEMENTS (metadata not useful in AsciiDoc output)
       ============================================================ -->

  <xsl:template match="db:colspec | db:spanspec"/>
  <xsl:template match="db:thead"/>  <!-- handled by table template -->
  <xsl:template match="db:tfoot"/>  <!-- handled by table template -->
  <xsl:template match="db:tgroup"/>  <!-- handled by table template - but we need rows -->
  <xsl:template match="db:caption"/>  <!-- handled inline -->

  <!-- abstract: render as a NOTE admonition block so the content is
       visible in the output rather than silently dropped. -->
  <xsl:template match="db:abstract">
    <xsl:text>&#10;[NOTE]&#10;====&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>====&#10;&#10;</xsl:text>
  </xsl:template>

  <!-- address block: just emit text -->
  <xsl:template match="db:address">
    <xsl:apply-templates/>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:street | db:city | db:state | db:postcode | db:country | db:otheraddr">
    <xsl:apply-templates/>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <!-- ============================================================
       DEFAULT TEXT NODE
       ============================================================ -->

  <!--
    In code/literal contexts, preserve whitespace exactly.
    In all other contexts, normalize whitespace runs to single spaces
    while preserving leading/trailing space characters for word boundaries
    (only when there is adjacent inline content).
  -->
  <xsl:template match="text()">
    <xsl:choose>
      <xsl:when test="ancestor::db:programlisting or ancestor::db:screen or
                      ancestor::db:literallayout or ancestor::db:synopsis or
                      ancestor::db:address or ancestor::db:funcsynopsis">
        <xsl:value-of select="."/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:variable name="norm" select="normalize-space(.)"/>
        <xsl:choose>
          <!-- Pure-whitespace text node between two sibling nodes: emit a single
               space so that adjacent inline elements (e.g. *cmd* `file`) are not
               fused together (which would break AsciiDoc constrained markup). -->
          <xsl:when test="string-length($norm) = 0 and
                          preceding-sibling::node() and
                          following-sibling::node()">
            <xsl:text> </xsl:text>
          </xsl:when>
          <xsl:otherwise>
            <!-- Only preserve leading space when there IS a preceding sibling node
                 (i.e., we are in the middle of inline content). -->
            <xsl:if test="preceding-sibling::node() and
                          string-length($norm) > 0 and
                          translate(substring(.,1,1),' &#9;&#10;&#13;','') = ''">
              <xsl:text> </xsl:text>
            </xsl:if>
            <xsl:value-of select="$norm"/>
            <!-- Preserve a single trailing space when there IS a following sibling node. -->
            <xsl:if test="following-sibling::node() and
                          string-length(.) > 1 and
                          string-length($norm) > 0 and
                          translate(substring(.,string-length(.),1),' &#9;&#10;&#13;','') = ''">
              <xsl:text> </xsl:text>
            </xsl:if>
          </xsl:otherwise>
        </xsl:choose>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <!-- ============================================================
       CATCHALL for unhandled elements: process children
       ============================================================ -->
  <xsl:template match="*">
    <xsl:apply-templates/>
  </xsl:template>

</xsl:stylesheet>
