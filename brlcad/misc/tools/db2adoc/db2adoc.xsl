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
  <xsl:preserve-space elements="db:programlisting db:screen db:literallayout db:synopsis db:address"/>

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
    <xsl:text>&#10;[NOTE]&#10;====&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>====&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:warning">
    <xsl:text>&#10;[WARNING]&#10;====&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>====&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:caution">
    <xsl:text>&#10;[CAUTION]&#10;====&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>====&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:tip">
    <xsl:text>&#10;[TIP]&#10;====&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>====&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:important">
    <xsl:text>&#10;[IMPORTANT]&#10;====&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>====&#10;&#10;</xsl:text>
  </xsl:template>

  <!-- ============================================================
       BLOCK QUOTES
       ============================================================ -->

  <xsl:template match="db:blockquote">
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
    <xsl:text>&#10;....&#10;</xsl:text>
    <xsl:value-of select="."/>
    <xsl:if test="substring(., string-length(.), 1) != '&#10;'">
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
    <xsl:text>....&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:literallayout">
    <xsl:text>&#10;....&#10;</xsl:text>
    <xsl:value-of select="."/>
    <xsl:if test="substring(., string-length(.), 1) != '&#10;'">
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
    <xsl:text>....&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:synopsis">
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
    <xsl:text>[example]&#10;====&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>====&#10;&#10;</xsl:text>
  </xsl:template>

  <!-- ============================================================
       LISTS
       ============================================================ -->

  <xsl:template match="db:itemizedlist">
    <xsl:param name="depth" select="0"/>
    <xsl:text>&#10;</xsl:text>
    <xsl:apply-templates select="db:listitem">
      <xsl:with-param name="depth" select="$depth"/>
      <xsl:with-param name="type" select="'bullet'"/>
    </xsl:apply-templates>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:orderedlist">
    <xsl:param name="depth" select="0"/>
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

  <xsl:template match="db:table | db:informaltable">
    <xsl:if test="db:title">
      <xsl:text>.</xsl:text>
      <xsl:value-of select="normalize-space(db:title)"/>
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
    <xsl:text>|===&#10;</xsl:text>
    <!-- Process thead rows first -->
    <xsl:apply-templates select=".//db:thead/db:row"/>
    <xsl:text>&#10;</xsl:text>
    <!-- Process tbody rows -->
    <xsl:apply-templates select=".//db:tbody/db:row"/>
    <xsl:text>|===&#10;&#10;</xsl:text>
  </xsl:template>

  <xsl:template match="db:row">
    <xsl:for-each select="db:entry">
      <xsl:text>|</xsl:text>
      <xsl:apply-templates/>
      <xsl:text> </xsl:text>
    </xsl:for-each>
    <xsl:text>&#10;</xsl:text>
  </xsl:template>

  <!-- ============================================================
       IMAGES / FIGURES
       ============================================================ -->

  <xsl:template match="db:figure | db:informalfigure">
    <xsl:if test="db:title">
      <xsl:text>.</xsl:text>
      <xsl:value-of select="normalize-space(db:title)"/>
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
    <xsl:apply-templates select=".//db:imagedata"/>
  </xsl:template>

  <xsl:template match="db:mediaobject">
    <xsl:apply-templates select=".//db:imagedata"/>
    <xsl:if test="db:caption">
      <xsl:text>.</xsl:text>
      <xsl:apply-templates select="db:caption/*"/>
      <xsl:text>&#10;</xsl:text>
    </xsl:if>
  </xsl:template>

  <xsl:template match="db:inlinemediaobject">
    <xsl:apply-templates select=".//db:imagedata"/>
  </xsl:template>

  <xsl:template match="db:imagedata">
    <xsl:text>image::</xsl:text>
    <xsl:value-of select="@fileref"/>
    <xsl:text>[</xsl:text>
    <xsl:if test="@width">
      <xsl:text>width=</xsl:text>
      <xsl:value-of select="@width"/>
    </xsl:if>
    <xsl:text>]&#10;&#10;</xsl:text>
  </xsl:template>

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
  <xsl:template match="db:tfoot"/>
  <xsl:template match="db:tgroup"/>  <!-- handled by table template - but we need rows -->
  <xsl:template match="db:caption"/>  <!-- handled inline -->
  <xsl:template match="db:abstract"/>

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
        <!-- Only preserve leading space when there IS a preceding sibling node
             (i.e., we are in the middle of inline content). -->
        <xsl:if test="preceding-sibling::node() and
                      string-length(normalize-space(.)) > 0 and
                      translate(substring(.,1,1),' &#9;&#10;&#13;','') = ''">
          <xsl:text> </xsl:text>
        </xsl:if>
        <xsl:value-of select="normalize-space(.)"/>
        <!-- Preserve a single trailing space when there IS a following sibling node. -->
        <xsl:if test="following-sibling::node() and
                      string-length(.) > 1 and
                      string-length(normalize-space(.)) > 0 and
                      translate(substring(.,string-length(.),1),' &#9;&#10;&#13;','') = ''">
          <xsl:text> </xsl:text>
        </xsl:if>
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
