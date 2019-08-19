<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet
  xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
  version="1.0">

  <xsl:variable name="schema-prefix">http://www.s1000d.org/S1000D_5-0/xml_schema_flat/</xsl:variable>
  
  <xsl:template match="@*|node()">
    <xsl:copy>
      <xsl:apply-templates select="@*|node()"/>
    </xsl:copy>
  </xsl:template>

  <xsl:template match="@xsi:noNamespaceSchemaLocation">
    <xsl:attribute name="xsi:noNamespaceSchemaLocation">
      <xsl:text>http://www.s1000d.org/S1000D_4-2/xml_schema_flat/</xsl:text>
      <xsl:value-of select="substring-after(., $schema-prefix)"/>
    </xsl:attribute>
  </xsl:template>

  <xsl:template match="dmTitle">
    <xsl:copy>
      <xsl:apply-templates select="techName"/>
      <xsl:choose>
        <xsl:when test="infoNameVariant">
          <infoName>
            <xsl:apply-templates select="infoName/node()"/>
            <xsl:text>, </xsl:text>
            <xsl:apply-templates select="infoNameVariant/node()"/>
          </infoName>
        </xsl:when>
        <xsl:when test="infoName">
          <xsl:apply-templates select="infoName"/>
        </xsl:when>
      </xsl:choose>
    </xsl:copy>
  </xsl:template>

</xsl:stylesheet>
