<xsl:stylesheet
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:fo="http://www.w3.org/1999/XSL/Format"
    version="1.0">
<xsl:output method="xml" version="1.0" indent="yes"/>
<xsl:param name="prefix"></xsl:param>
<xsl:template match="/">
  <book title="QOF design and developers reference"
        name="qof"
        link="index.html">
  <chapters>
     <sub name="Modules" link="{$prefix}modules.html"/>
     <sub name="Data Structures" link="{$prefix}classes.html"/>
     <sub name="Files" link="{$prefix}files.html"/>
     <sub name="Related Pages" link="{$prefix}pages.html"/>
  </chapters>
  <functions>
    <xsl:apply-templates select="doxygenindex/compound[@kind='group']/member[@kind='function']"/>
  </functions>
  </book>
</xsl:template>
<xsl:template match="member">
  <xsl:param name="name"><xsl:value-of select="name"/></xsl:param>
  <xsl:param name="refid"><xsl:value-of select="@refid"/></xsl:param>
  <xsl:param name="before"><xsl:value-of select="substring-before($refid,'_1')"/></xsl:param>
  <xsl:param name="after"><xsl:value-of select="substring-after($refid,'_1')"/></xsl:param>
  <xsl:param name="link"><xsl:value-of select="$before"/>.html#<xsl:value-of select="$after"/></xsl:param>
  <xsl:if test="starts-with($name,'qof') or starts-with($name, 'Qof')">
    <xsl:if test="starts-with($refid,'group__') and contains($refid, '_1')">
       <function name="{$name}" link="{$prefix}{$link}"/>
    </xsl:if>
  </xsl:if>
</xsl:template>
</xsl:stylesheet>
