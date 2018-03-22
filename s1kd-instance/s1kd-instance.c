#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/transform.h>
#include <libexslt/exslt.h>

#include "strings.h"
#include "xsl.h"

/* Prefix before errors printed to console */
#define ERR_PREFIX "s1kd-instance: ERROR: "

/* Error codes */
#define EXIT_MISSING_ARGS 1 /* Option or parameter missing */
#define EXIT_MISSING_FILE 2 /* File does not exist */
#define EXIT_BAD_APPLIC 4 /* Malformed applic definitions */
#define EXIT_NO_OVERWRITE 5 /* Did not overwrite existing out file */
#define EXIT_BAD_XML 6 /* Invalid XML/S1000D */
#define EXIT_BAD_ARG 7 /* Malformed argument */
#define EXIT_BAD_DATE 8 /* Malformed issue date */

/* When using the -g option, these are set as the values for the
 * originator.
 */
#define DEFAULT_ORIG_CODE "S1KDI"
#define DEFAULT_ORIG_NAME "s1kd-instance tool"

/* Convenient structure for all strings related to uniquely identifying a
 * data module or pub module.
 */
struct ident {
	bool extended;
	bool ispm;
	char *extensionProducer;
	char *extensionCode;
	char *modelIdentCode;
	char *systemDiffCode;
	char *systemCode;
	char *subSystemCode;
	char *subSubSystemCode;
	char *assyCode;
	char *disassyCode;
	char *disassyCodeVariant;
	char *infoCode;
	char *infoCodeVariant;
	char *itemLocationCode;
	char *learnCode;
	char *learnEventCode;
	char *pmIssuer;
	char *pmNumber;
	char *pmVolume;
	char *issueNumber;
	char *inWork;
	char *languageIsoCode;
	char *countryIsoCode;
};

xmlNodePtr applicability;

int napplics = 0;

void define_applic(char *ident, char *type, char *value)
{
	xmlNodePtr assert = NULL;
	xmlNodePtr cur;

	for (cur = applicability->children; cur; cur = cur->next) {
		char *cur_ident = (char *) xmlGetProp(cur, BAD_CAST "applicPropertyIdent");
		char *cur_type  = (char *) xmlGetProp(cur, BAD_CAST "applicPropertyType");
		char *cur_value = (char *) xmlGetProp(cur, BAD_CAST "applicPropertyValues");

		if (strcmp(cur_ident, ident) == 0 && strcmp(cur_type, type) == 0) {
			assert = cur;
		}

		xmlFree(cur_ident);
		xmlFree(cur_type);
		xmlFree(cur_value);
	}

	if (!assert) {
		assert = xmlNewChild(applicability, NULL, BAD_CAST "assert", NULL);
		xmlSetProp(assert, BAD_CAST "applicPropertyIdent", BAD_CAST ident);
		xmlSetProp(assert, BAD_CAST "applicPropertyType",  BAD_CAST type);
		xmlSetProp(assert, BAD_CAST "applicPropertyValues", BAD_CAST value);
	} else {
		if (xmlHasProp(assert, BAD_CAST "applicPropertyValues")) {
			xmlChar *first_value = xmlGetProp(assert, BAD_CAST "applicPropertyValues");
			xmlNewChild(assert, NULL, BAD_CAST "value", first_value);
			xmlUnsetProp(assert, BAD_CAST "applicPropertyValues");
		}
		xmlNewChild(assert, NULL, BAD_CAST "value", BAD_CAST value);
		--napplics;
	}
}

/* Find the first child element with a given name */
xmlNodePtr find_child(xmlNodePtr parent, const char *name)
{
	xmlNodePtr cur;

	for (cur = parent->children; cur; cur = cur->next) {
		if (strcmp((char *) cur->name, name) == 0) {
			return cur;
		}
	}

	return NULL;
}

/* Same as above but throws a fatal error if the child is not found instead of returning NULL. */
xmlNodePtr find_req_child(xmlNodePtr parent, const char *name)
{
	xmlNodePtr child;

	if (!(child = find_child(parent, name))) {
		fprintf(stderr, ERR_PREFIX "Element %s missing child element %s.\n", (char *) parent->name, name);
		exit(EXIT_BAD_XML);
	}

	return child;
}

xmlNodePtr first_xpath_node(xmlDocPtr doc, char *path)
{
	xmlXPathContextPtr ctxt = xmlXPathNewContext(doc);
	xmlXPathObjectPtr result = xmlXPathEvalExpression(BAD_CAST path, ctxt);
	xmlNodePtr ret;
	
	if (xmlXPathNodeSetIsEmpty(result->nodesetval)) {
		ret = NULL;
	} else {
		ret = result->nodesetval->nodeTab[0];
	}

	xmlXPathFreeObject(result);
	xmlXPathFreeContext(ctxt);

	return ret;
}

/* Copy strings related to uniquely identifying a data module. The strings are
 * dynamically allocated so they must be freed using free_ident. */
void init_ident(struct ident *ident, xmlDocPtr doc)
{
	xmlNodePtr moduleIdent, identExtension, code, language, issueInfo;

	moduleIdent = first_xpath_node(doc, "//dmIdent|//pmIdent");

	ident->ispm = strcmp((char *) moduleIdent->name, "pmIdent") == 0;

	identExtension = first_xpath_node(doc, "//dmIdent/identExtension|//pmIdent/identExtension");
	code = first_xpath_node(doc, "//dmIdent/dmCode|//pmIdent/pmCode");
	language = first_xpath_node(doc, "//dmIdent/language|//pmIdent/language");
	issueInfo = first_xpath_node(doc, "//dmIdent/issueInfo|//pmIdent/issueInfo");

	ident->modelIdentCode     = (char *) xmlGetProp(code, BAD_CAST "modelIdentCode");

	if (!ident->ispm) {
		ident->systemDiffCode     = (char *) xmlGetProp(code, BAD_CAST "systemDiffCode");
		ident->systemCode         = (char *) xmlGetProp(code, BAD_CAST "systemCode");
		ident->subSystemCode      = (char *) xmlGetProp(code, BAD_CAST "subSystemCode");
		ident->subSubSystemCode   = (char *) xmlGetProp(code, BAD_CAST "subSubSystemCode");
		ident->assyCode           = (char *) xmlGetProp(code, BAD_CAST "assyCode");
		ident->disassyCode        = (char *) xmlGetProp(code, BAD_CAST "disassyCode");
		ident->disassyCodeVariant = (char *) xmlGetProp(code, BAD_CAST "disassyCodeVariant");
		ident->infoCode           = (char *) xmlGetProp(code, BAD_CAST "infoCode");
		ident->infoCodeVariant    = (char *) xmlGetProp(code, BAD_CAST "infoCodeVariant");
		ident->itemLocationCode   = (char *) xmlGetProp(code, BAD_CAST "itemLocationCode");
		ident->learnCode          = (char *) xmlGetProp(code, BAD_CAST "learnCode");
		ident->learnEventCode     = (char *) xmlGetProp(code, BAD_CAST "learnEventCode");
	} else {
		ident->pmIssuer           = (char *) xmlGetProp(code, BAD_CAST "pmIssuer");
		ident->pmNumber           = (char *) xmlGetProp(code, BAD_CAST "pmNumber");
		ident->pmVolume           = (char *) xmlGetProp(code, BAD_CAST "pmVolume");
	}

	ident->issueNumber = (char *) xmlGetProp(issueInfo, BAD_CAST "issueNumber");
	ident->inWork      = (char *) xmlGetProp(issueInfo, BAD_CAST "inWork");

	ident->languageIsoCode = (char *) xmlGetProp(language, BAD_CAST "languageIsoCode");
	ident->countryIsoCode  = (char *) xmlGetProp(language, BAD_CAST "countryIsoCode");

	if (identExtension) {
		ident->extended = true;
		ident->extensionProducer = (char *) xmlGetProp(identExtension, BAD_CAST "extensionProducer");
		ident->extensionCode     = (char *) xmlGetProp(identExtension, BAD_CAST "extensionCode");
	} else {
		ident->extended = false;
	}
}

void free_ident(struct ident *ident)
{
	if (ident->extended) {
		xmlFree(ident->extensionProducer);
		xmlFree(ident->extensionCode);
	}

	xmlFree(ident->modelIdentCode);

	if (!ident->ispm) {
		xmlFree(ident->systemDiffCode);
		xmlFree(ident->systemCode);
		xmlFree(ident->subSystemCode);
		xmlFree(ident->subSubSystemCode);
		xmlFree(ident->assyCode);
		xmlFree(ident->disassyCode);
		xmlFree(ident->disassyCodeVariant);
		xmlFree(ident->infoCode);
		xmlFree(ident->infoCodeVariant);
		xmlFree(ident->itemLocationCode);
		xmlFree(ident->learnCode);
		xmlFree(ident->learnEventCode);
	} else {
		xmlFree(ident->pmIssuer);
		xmlFree(ident->pmNumber);
		xmlFree(ident->pmVolume);
	}

	xmlFree(ident->issueNumber);
	xmlFree(ident->inWork);
	xmlFree(ident->languageIsoCode);
	xmlFree(ident->countryIsoCode);
}

/* Evaluate an applic statement, returning whether it is valid or invalid given
 * the user-supplied applicability settings.
 *
 * If assume is true, undefined attributes and conditions are ignored. This is
 * primarily useful for determining which elements are not applicable in content
 * and may be removed.
 *
 * If assume is false, undefined attributes or conditions will cause an applic
 * statement to evaluate as invalid. This is primarily useful for determining
 * which applic statements and references are unambigously true (they do not
 * rely on any undefined attributes or conditions) and therefore may be removed.
 *
 * An undefined attribute/condition is a product attribute (ACT) or
 * condition (CCT) for which a value is asserted in the applic statement but
 * for which no value was supplied by the user.
 */
bool eval_applic(xmlNodePtr node, bool assume);

/* Tests whether a value is in an S1000D range (a~c is equivalent to a|b|c) */
bool is_in_range(const char *value, const char *range)
{
	char *ran;
	char *first;
	char *last;
	bool ret;

	if (!strchr(range, '~')) {
		return strcmp(value, range) == 0;
	}

	ran = malloc(strlen(range) + 1);

	strcpy(ran, range);

	first = strtok(ran, "~");
	last = strtok(NULL, "~");

	ret = strcmp(value, first) >= 0 && strcmp(value, last) <= 0;

	free(ran);

	return ret;
}

/* Tests whether a value is in an S1000D set (a|b|c) */
bool is_in_set(const char *value, const char *set)
{
	char *s;
	char *val = NULL;
	bool ret = false;

	if (!strchr(set, '|')) {
		return is_in_range(value, set);
	}

	s = malloc(strlen(set) + 1);

	strcpy(s, set);

	while ((val = strtok(val ? NULL : s, "|"))) {
		if (is_in_range(value, val)) {
			ret = true;
			break;
		}
	}

	free(s);

	return ret;
}

/* Evaluate multiple values for a property */
bool eval_multi(xmlNodePtr multi, const char *ident, const char *type, const char *value)
{
	xmlNodePtr cur;
	bool result = false;

	for (cur = multi->children; cur; cur = cur->next) {
		char *cur_value = (char *) xmlNodeGetContent(cur);

		if (is_in_set(cur_value, value)) {
			result = true;
			break;
		}
	}

	return result;
}

/* Tests whether ident:type=value was defined by the user */
bool is_applic(const char *ident, const char *type, const char *value, bool assume)
{
	xmlNodePtr cur;

	bool result = assume;

	if (!(ident || type || value)) {
		return assume;
	}

	for (cur = applicability->children; cur; cur = cur->next) {
		char *cur_ident = (char *) xmlGetProp(cur, BAD_CAST "applicPropertyIdent");
		char *cur_type  = (char *) xmlGetProp(cur, BAD_CAST "applicPropertyType");
		char *cur_value = (char *) xmlGetProp(cur, BAD_CAST "applicPropertyValues");

		bool match = strcmp(cur_ident, ident) == 0 && strcmp(cur_type, type) == 0;

		if (match) {
			if (cur_value) {
				result = is_in_set(cur_value, value);
			} else {
				result = result && eval_multi(cur, ident, type, value);
			}
		}

		xmlFree(cur_ident);
		xmlFree(cur_type);
		xmlFree(cur_value);

		if (match) {
			break;
		}

	}

	return result;
}

/* Tests whether an <assert> element is applicable */
bool eval_assert(xmlNodePtr assert, bool assume)
{
	char *ident, *type, *values;

	bool ret;

	ident  = (char *) xmlGetProp(assert, BAD_CAST "applicPropertyIdent");
	type   = (char *) xmlGetProp(assert, BAD_CAST "applicPropertyType");
	values = (char *) xmlGetProp(assert, BAD_CAST "applicPropertyValues");

	ret = is_applic(ident, type, values, assume);

	xmlFree(ident);
	xmlFree(type);
	xmlFree(values);

	return ret;
}

/* Test whether an <evaluate> element is applicable. */
bool eval_evaluate(xmlNodePtr evaluate, bool assume)
{
	char *op;

	bool ret;

	xmlNodePtr cur;

	op = (char *) xmlGetProp(evaluate, BAD_CAST "andOr");

	if (!op) {
		fprintf(stderr, ERR_PREFIX "Element evaluate missing required attribute andOr.");
		exit(EXIT_BAD_XML);
	}

	ret = strcmp(op, "and") == 0;

	for (cur = evaluate->children; cur; cur = cur->next) {
		if (strcmp((char *) cur->name, "assert") == 0 || strcmp((char *) cur->name, "evaluate") == 0) {
			if (strcmp(op, "and") == 0) {
				ret = ret && eval_applic(cur, assume);
			} else if (strcmp(op, "or") == 0) {
				ret = ret || eval_applic(cur, assume);
			}
		}
	}

	xmlFree(op);

	return ret;
}

/* Generic test for either <assert> or <evaluate> */
bool eval_applic(xmlNodePtr node, bool assume)
{
	if (strcmp((char *) node->name, "assert") == 0) {
		return eval_assert(node, assume);
	} else if (strcmp((char *) node->name, "evaluate") == 0) {
		return eval_evaluate(node, assume);
	}

	return false;
}

/* Tests whether an <applic> element is true. */
bool eval_applic_stmt(xmlNodePtr applic, bool assume)
{
	xmlNodePtr stmt;

	stmt = find_child(applic, "assert");

	if (!stmt) {
		stmt = find_child(applic, "evaluate");
	}

	if (!stmt) {
		return assume;
	}

	return eval_applic(stmt, assume);
}

/* Search recursively for a descendant element with the given id */
xmlNodePtr get_element_by_id(xmlNodePtr root, const char *id)
{
	xmlNodePtr cur;
	char *cid;

	for (cur = root->children; cur; cur = cur->next) {
		xmlNodePtr ch;
		bool match;

		cid = (char *) xmlGetProp(cur, BAD_CAST "id");

		match = cid && strcmp(cid, id) == 0;

		xmlFree(cid);

		if (match) {
			return cur;
		} else if ((ch = get_element_by_id(cur, id))) {
			return ch;
		}
	}

	return NULL;
}

/* Remove non-applicable elements from content */
void strip_applic(xmlNodePtr referencedApplicGroup, xmlNodePtr node)
{
	xmlNodePtr cur, next;

	if (xmlHasProp(node, BAD_CAST "applicRefId")) {
		char *applicRefId;
		xmlNodePtr applic;

		applicRefId = (char *) xmlGetProp(node, BAD_CAST "applicRefId");
		applic = get_element_by_id(referencedApplicGroup, applicRefId);
		xmlFree(applicRefId);

		if (applic && !eval_applic_stmt(applic, true)) {
			xmlUnlinkNode(node);
			xmlFreeNode(node);
			return;
		}
	}

	cur = node->children;
	while (cur) {
		next = cur->next;
		strip_applic(referencedApplicGroup, cur);
		cur = next;
	}
}

/* Remove applic references on content where all assertions are unambigously true */
void clean_applic(xmlNodePtr referencedApplicGroup, xmlNodePtr node)
{
	xmlNodePtr cur;

	if (xmlHasProp(node, BAD_CAST "applicRefId")) {
		char *applicRefId;
		xmlNodePtr applic;

		applicRefId = (char *) xmlGetProp(node, BAD_CAST "applicRefId");
		applic = get_element_by_id(referencedApplicGroup, applicRefId);
		xmlFree(applicRefId);

		if (applic && eval_applic_stmt(applic, false)) {
			xmlUnsetProp(node, BAD_CAST "applicRefId");
		}
	}

	for (cur = node->children; cur; cur = cur->next) {
		clean_applic(referencedApplicGroup, cur);
	}
}

/* Remove applic statements or parts of applic statements where all assertions
 * are unambigously true */
void simpl_applic(xmlNodePtr node)
{
	xmlNodePtr cur, next;

	if (strcmp((char *) node->name, "applic") == 0) {
		if (eval_applic_stmt(node, false) || !eval_applic_stmt(node, true)) {
			xmlUnlinkNode(node);
			xmlFreeNode(node);
			return;
		}
	} else if (strcmp((char *) node->name, "evaluate") == 0) {
		if (eval_applic(node, false) || !eval_applic(node, true)) {
			xmlUnlinkNode(node);
			xmlFreeNode(node);
			return;
		}
	} else if (strcmp((char *) node->name, "assert") == 0) {
		char *ident  = (char *) xmlGetProp(node, BAD_CAST "applicPropertyIdent");
		char *type   = (char *) xmlGetProp(node, BAD_CAST "applicPropertyType");
		char *values = (char *) xmlGetProp(node, BAD_CAST "applicPropertyValues");

		bool uneeded = is_applic(ident, type, values, false) || !is_applic(ident, type, values, true);

		xmlFree(ident);
		xmlFree(type);
		xmlFree(values);

		if (uneeded) {
			xmlUnlinkNode(node);
			xmlFreeNode(node);
			return;
		}
	}

	cur = node->children;
	while (cur) {
		next = cur->next;
		simpl_applic(cur);
		cur = next;
	}
}

/* If an <evaluate> contains only one (or no) child elements, remove it. */
void simpl_evaluate(xmlNodePtr evaluate)
{
	int nchild = 0;
	xmlNodePtr cur;

	for (cur = evaluate->children; cur; cur = cur->next) {
		if (cur->type == XML_ELEMENT_NODE) {
			++nchild;
		}
	}

	if (nchild < 2) {
		xmlNodePtr child;

		child = find_child(evaluate, "assert");
		if (!child) child = find_child(evaluate, "evaluate");
		xmlAddNextSibling(evaluate, child);
		xmlUnlinkNode(evaluate);
		xmlFreeNode(evaluate);
	}
}

/* Simplify <evaluate> elements recursively. */
void simpl_applic_evals(xmlNodePtr node)
{
	xmlNodePtr cur, next;

	cur = node->children;
	while (cur) {
		next = cur->next;
		if (cur->type == XML_ELEMENT_NODE) {
			simpl_applic_evals(cur);
		}
		cur = next;
	}

	if (strcmp((char *) node->name, "evaluate") == 0) {
		simpl_evaluate(node);
	}
}

/* Remove <referencedApplicGroup> if all applic statements are removed */
void simpl_applic_clean(xmlNode* referencedApplicGroup)
{
	bool has_applic = false;
	xmlNodePtr cur;

	if (!referencedApplicGroup) {
		return;
	}

	simpl_applic(referencedApplicGroup);
	simpl_applic_evals(referencedApplicGroup);

	for (cur = referencedApplicGroup->children; cur; cur = cur->next) {
		if (strcmp((char *) cur->name, "applic") == 0) {
			has_applic = true;
		}
	}

	if (!has_applic) {
		xmlUnlinkNode(referencedApplicGroup);
		xmlFreeNode(referencedApplicGroup);
	}
}


/* Add metadata linking the data module instance with the master data module */
void add_source(xmlDocPtr doc)
{
	xmlNodePtr ident, status, sourceIdent, node, cur;

	ident       = first_xpath_node(doc, "//dmIdent|//pmIdent");
	status      = first_xpath_node(doc, "//dmStatus|//pmStatus");
	sourceIdent = first_xpath_node(doc, "//dmStatus/sourceDmIdent|//pmStatus/sourcePmIdent");
	node        = first_xpath_node(doc, "(//dmStatus/repositorySourceDmIdent|//dmStatus/security|//pmStatus/security)[1]");

	if (sourceIdent) {
		xmlUnlinkNode(sourceIdent);
		xmlFreeNode(sourceIdent);
	}

	if (strcmp((char *) status->name, "dmStatus") == 0) {
		sourceIdent = xmlNewNode(NULL, BAD_CAST "sourceDmIdent");
	} else {
		sourceIdent = xmlNewNode(NULL, BAD_CAST "sourcePmIdent");
	}

	sourceIdent = xmlAddPrevSibling(node, sourceIdent);

	for (cur = ident->children; cur; cur = cur->next) {
		xmlAddChild(sourceIdent, xmlCopyNode(cur, 1));
	}
}

/* Add an extension to the data module code */
void set_extd(xmlDocPtr doc, const char *extension)
{
	xmlNodePtr identExtension, code;
	char *ext, *extensionProducer, *extensionCode;

	identExtension = first_xpath_node(doc, "//dmIdent/identExtension|//pmIdent/identExtension");
	code = first_xpath_node(doc, "//dmIdent/dmCode|//pmIdent/pmCode");

	ext = strdup(extension);

	if (!identExtension) {
		identExtension = xmlNewNode(NULL, BAD_CAST "identExtension");
		identExtension = xmlAddPrevSibling(code, identExtension);
	}

	extensionProducer = strtok(ext, "-");
	extensionCode     = strtok(NULL, "-");

	xmlSetProp(identExtension, BAD_CAST "extensionProducer", BAD_CAST extensionProducer);
	xmlSetProp(identExtension, BAD_CAST "extensionCode",     BAD_CAST extensionCode);

	free(ext);
}

/* Set the DMC of the produced data module of the instance */
void set_code(xmlDocPtr doc, const char *new_code)
{
	xmlNodePtr code;

	char modelIdentCode[15];
	char systemDiffCode[5];
	char systemCode[4];
	char subSystemCode[2];
	char subSubSystemCode[2];
	char assyCode[5];
	char disassyCode[3];
	char disassyCodeVariant[4];
	char infoCode[4];
	char infoCodeVariant[2];
	char itemLocationCode[2];
	char learnCode[4];
	char learnEventCode[2];

	int n;

	code = first_xpath_node(doc, "//dmIdent/dmCode|//pmIdent/pmCode");

	n = sscanf(new_code,
		"%14[^-]-%4[^-]-%3[^-]-%1s%1s-%4[^-]-%2s%3[^-]-%3s%1s-%1s-%3s%1s",
		modelIdentCode,
		systemDiffCode,
		systemCode,
		subSystemCode,
		subSubSystemCode,
		assyCode,
		disassyCode,
		disassyCodeVariant,
		infoCode,
		infoCodeVariant,
		itemLocationCode,
		learnCode,
		learnEventCode);

	if (n != 11 && n != 13) {
		char pmIssuer[6];
		char pmNumber[6];
		char pmVolume[3];

		n = sscanf(new_code,
			"%14[^-]-%5[^-]-%5[^-]-%2s",
			modelIdentCode,
			pmIssuer,
			pmNumber,
			pmVolume);
		
		if (n != 4) {
			fprintf(stderr, ERR_PREFIX "Bad data module/pub module code: %s.\n", new_code);
			exit(EXIT_BAD_ARG);
		} else {
			xmlSetProp(code, BAD_CAST "modelIdentCode", BAD_CAST modelIdentCode);
			xmlSetProp(code, BAD_CAST "pmIssuer", BAD_CAST pmIssuer);
			xmlSetProp(code, BAD_CAST "pmNumber", BAD_CAST pmNumber);
			xmlSetProp(code, BAD_CAST "pmVolume", BAD_CAST pmVolume);
		}
	} else {
		xmlSetProp(code, BAD_CAST "modelIdentCode",     BAD_CAST modelIdentCode);
		xmlSetProp(code, BAD_CAST "systemDiffCode",     BAD_CAST systemDiffCode);
		xmlSetProp(code, BAD_CAST "systemCode",         BAD_CAST systemCode);
		xmlSetProp(code, BAD_CAST "subSystemCode",      BAD_CAST subSystemCode);
		xmlSetProp(code, BAD_CAST "subSubSystemCode",   BAD_CAST subSubSystemCode);
		xmlSetProp(code, BAD_CAST "assyCode",           BAD_CAST assyCode);
		xmlSetProp(code, BAD_CAST "disassyCode",        BAD_CAST disassyCode);
		xmlSetProp(code, BAD_CAST "disassyCodeVariant", BAD_CAST disassyCodeVariant);
		xmlSetProp(code, BAD_CAST "infoCode",           BAD_CAST infoCode);
		xmlSetProp(code, BAD_CAST "infoCodeVariant",    BAD_CAST infoCodeVariant);
		xmlSetProp(code, BAD_CAST "itemLocationCode",   BAD_CAST itemLocationCode);

		if (n == 13) {
			xmlSetProp(code, BAD_CAST "learnCode", BAD_CAST learnCode);
			xmlSetProp(code, BAD_CAST "learnEventCode", BAD_CAST learnEventCode);
		}
	}
}

/* Set the techName and/or infoName of the data module instance */
void set_title(xmlDocPtr doc, char *tech, char *info)
{
	xmlNodePtr dmTitle, techName, infoName;
	
	dmTitle  = first_xpath_node(doc, "//dmAddressItems/dmTitle");
	techName = first_xpath_node(doc, "//dmAddressItems/dmTitle/techName|//pmAddressItems/pmTitle");
	infoName = first_xpath_node(doc, "//dmAddressItems/dmTitle/infoName");

	if (strcmp(tech, "") != 0) {
		xmlNodeSetContent(techName, BAD_CAST tech);
	}

	if (strcmp(info, "") != 0) {
		if (!infoName) {
			infoName = xmlNewChild(dmTitle, NULL, BAD_CAST "infoName", NULL);
		}
		xmlNodeSetContent(infoName, BAD_CAST info);
	}	
}

xmlNodePtr create_assert(xmlChar *ident, xmlChar *type, xmlChar *values)
{
	xmlNodePtr assert;

	assert = xmlNewNode(NULL, BAD_CAST "assert");

	xmlSetProp(assert, BAD_CAST "applicPropertyIdent", ident);
	xmlSetProp(assert, BAD_CAST "applicPropertyType", type);
	xmlSetProp(assert, BAD_CAST "applicPropertyValues", values);

	return assert;
}

xmlNodePtr create_or(xmlChar *ident, xmlChar *type, xmlNodePtr values)
{
	xmlNodePtr or, cur;

	or = xmlNewNode(NULL, BAD_CAST "evaluate");
	xmlSetProp(or, BAD_CAST "andOr", BAD_CAST "or");

	for (cur = values->children; cur; cur = cur->next) {
		xmlChar *value;

		value = xmlNodeGetContent(cur);
		xmlAddChild(or, create_assert(ident, type, value));
		xmlFree(value);
	}

	return or;
}

/* Set the applicability for the whole datamodule instance */
void set_applic(xmlDocPtr doc, char *new_text)
{
	xmlNodePtr new_applic;
	xmlNodePtr new_displayText;
	xmlNodePtr new_simplePara;
	xmlNodePtr new_evaluate;

	xmlNodePtr cur;

	xmlNodePtr applic;

	applic = first_xpath_node(doc, "//dmStatus/applic|//pmStatus/applic");

	new_applic = xmlNewNode(NULL, BAD_CAST "applic");
	xmlAddNextSibling(applic, new_applic);

	if (strcmp(new_text, "") != 0) {
		new_displayText = xmlNewChild(new_applic, NULL, BAD_CAST "displayText", NULL);
		new_simplePara = xmlNewChild(new_displayText, NULL, BAD_CAST "simplePara", NULL);
		xmlNodeSetContent(new_simplePara, BAD_CAST new_text);
	}

	if (napplics > 1) {
		new_evaluate = xmlNewChild(new_applic, NULL, BAD_CAST "evaluate", NULL);
		xmlSetProp(new_evaluate, BAD_CAST "andOr", BAD_CAST "and");
	} else {
		new_evaluate = new_applic;
	}

	for (cur = applicability->children; cur; cur = cur->next) {
		xmlChar *cur_ident = xmlGetProp(cur, BAD_CAST "applicPropertyIdent");
		xmlChar *cur_type  = xmlGetProp(cur, BAD_CAST "applicPropertyType");
		xmlChar *cur_value = xmlGetProp(cur, BAD_CAST "applicPropertyValues");

		if (cur_value) {
			xmlAddChild(new_evaluate, create_assert(cur_ident, cur_type, cur_value));
		} else {
			xmlAddChild(new_evaluate, create_or(cur_ident, cur_type, cur));
		}

		xmlFree(cur_ident);
		xmlFree(cur_type);
		xmlFree(cur_value);
	}

	xmlUnlinkNode(applic);
	xmlFreeNode(applic);
}

/* Set the language/country for the data module instance */
void set_lang(xmlDocPtr doc, char *lang)
{
	xmlNodePtr language;

	char *language_iso_code;
	char *country_iso_code;

	int i;

	language = first_xpath_node(doc, "//dmIdent/language|//pmIdent/language");

	language_iso_code = strtok(lang, "-");
	country_iso_code = strtok(NULL, "");

	for (i = 0; language_iso_code[i]; ++i) {
		language_iso_code[i] = tolower(language_iso_code[i]);
	}
	for (i = 0; country_iso_code[i]; ++i) {
		country_iso_code[i] = toupper(country_iso_code[i]);
	}

	xmlSetProp(language, BAD_CAST "languageIsoCode", BAD_CAST language_iso_code);
	xmlSetProp(language, BAD_CAST "countryIsoCode", BAD_CAST country_iso_code);
}

void auto_name(char *out, xmlDocPtr dm, const char *dir, bool noiss)
{
	struct ident ident;
	int i;
	char iss[8] = "";

	init_ident(&ident, dm);

	for (i = 0; ident.languageIsoCode[i]; ++i) {
		ident.languageIsoCode[i] = toupper(ident.languageIsoCode[i]);
	}

	if (!noiss) {
		sprintf(iss, "_%s-%s", ident.issueNumber, ident.inWork);
	}

	if (ident.ispm) {
		if (ident.extended) {
			sprintf(out, "%s/PME-%s-%s-%s-%s-%s-%s%s_%s-%s.XML",
				dir,
				ident.extensionProducer,
				ident.extensionCode,
				ident.modelIdentCode,
				ident.pmIssuer,
				ident.pmNumber,
				ident.pmVolume,
				iss,
				ident.languageIsoCode,
				ident.countryIsoCode);
		} else {
			sprintf(out, "%s/PMC-%s-%s-%s-%s%s_%s-%s.XML",
				dir,
				ident.modelIdentCode,
				ident.pmIssuer,
				ident.pmNumber,
				ident.pmVolume,
				iss,
				ident.languageIsoCode,
				ident.countryIsoCode);
		}
	} else {
		char learn[6] = "";

		if (ident.learnCode && ident.learnEventCode) {
			sprintf(learn, "-%s%s", ident.learnCode, ident.learnEventCode);
		}

		if (ident.extended) {
			sprintf(out, "%s/DME-%s-%s-%s-%s-%s-%s%s-%s-%s%s-%s%s-%s%s%s_%s-%s.XML",
				dir,
				ident.extensionProducer,
				ident.extensionCode,
				ident.modelIdentCode,
				ident.systemDiffCode,
				ident.systemCode,
				ident.subSystemCode,
				ident.subSubSystemCode,
				ident.assyCode,
				ident.disassyCode,
				ident.disassyCodeVariant,
				ident.infoCode,
				ident.infoCodeVariant,
				ident.itemLocationCode,
				learn,
				iss,
				ident.languageIsoCode,
				ident.countryIsoCode);
		} else {
			sprintf(out, "%s/DMC-%s-%s-%s-%s%s-%s-%s%s-%s%s-%s%s%s_%s-%s.XML",
				dir,
				ident.modelIdentCode,
				ident.systemDiffCode,
				ident.systemCode,
				ident.subSystemCode,
				ident.subSubSystemCode,
				ident.assyCode,
				ident.disassyCode,
				ident.disassyCodeVariant,
				ident.infoCode,
				ident.infoCodeVariant,
				ident.itemLocationCode,
				learn,
				iss,
				ident.languageIsoCode,
				ident.countryIsoCode);
		}
	}

	free_ident(&ident);
}

/* Add an "identity" template to an XSL stylesheet */
void add_identity(xmlDocPtr style)
{
	xmlDocPtr identity;
	xmlNodePtr stylesheet, first, template;

	identity = xmlReadMemory((const char *) ___common_identity_xsl, ___common_identity_xsl_len, NULL, NULL, 0);
	template = xmlFirstElementChild(xmlDocGetRootElement(identity));

	stylesheet = xmlDocGetRootElement(style);

	first = xmlFirstElementChild(stylesheet);

	if (first) {
		xmlAddPrevSibling(first, xmlCopyNode(template, 1));
	} else {
		xmlAddChild(stylesheet, xmlCopyNode(template, 1));
	}

	xmlFreeDoc(identity);
}

/* Get the appropriate built-in CIR repository XSLT by name */
bool get_cir_xsl(const char *cirtype, unsigned char **xsl, unsigned int *len)
{
	if (strcmp(cirtype, "accessPointRepository") == 0) {
		*xsl = cirxsl_accessPointRepository_xsl;
		*len = cirxsl_accessPointRepository_xsl_len;
	} else if (strcmp(cirtype, "applicRepository") == 0) {
		*xsl = cirxsl_applicRepository_xsl;
		*len = cirxsl_applicRepository_xsl_len;
	} else if (strcmp(cirtype, "cautionRepository") == 0) {
		*xsl = cirxsl_cautionRepository_xsl;
		*len = cirxsl_cautionRepository_xsl_len;
	} else if (strcmp(cirtype, "circuitBreakerRepository") == 0) {
		*xsl = cirxsl_circuitBreakerRepository_xsl;
		*len = cirxsl_circuitBreakerRepository_xsl_len;
	} else if (strcmp(cirtype, "controlIndicatorRepository") == 0) {
		*xsl = cirxsl_controlIndicatorRepository_xsl;
		*len = cirxsl_controlIndicatorRepository_xsl_len;
	} else if (strcmp(cirtype, "enterpriseRepository") == 0) {
		*xsl = cirxsl_enterpriseRepository_xsl;
		*len = cirxsl_enterpriseRepository_xsl_len;
	} else if (strcmp(cirtype, "functionalItemRepository") == 0) {
		*xsl = cirxsl_functionalItemRepository_xsl;
		*len = cirxsl_functionalItemRepository_xsl_len;
	} else if (strcmp(cirtype, "partRepository") == 0) {
		*xsl = cirxsl_partRepository_xsl;
		*len = cirxsl_partRepository_xsl_len;
	} else if (strcmp(cirtype, "supplyRepository") == 0) {
		*xsl = cirxsl_supplyRepository_xsl;
		*len = cirxsl_supplyRepository_xsl_len;
	} else if (strcmp(cirtype, "toolRepository") == 0) {
		*xsl = cirxsl_toolRepository_xsl;
		*len = cirxsl_toolRepository_xsl_len;
	} else if (strcmp(cirtype, "warningRepository") == 0) {
		*xsl = cirxsl_warningRepository_xsl;
		*len = cirxsl_warningRepository_xsl_len;
	} else if (strcmp(cirtype, "zoneRepository") == 0) {
		*xsl = cirxsl_zoneRepository_xsl;
		*len = cirxsl_zoneRepository_xsl_len;
	} else {
		fprintf(stderr, ERR_PREFIX "No built-in XSLT for CIR type: %s\n", cirtype);
		return false;
	}

	return true;
}

/* Dump built-in XSLT for resolving CIR repository dependencies */
void dump_cir_xsl(const char *repo)
{
	unsigned char *xsl;
	unsigned int len;

	if (get_cir_xsl(repo, &xsl, &len)) {
		printf("%.*s", len, xsl);
	} else {
		exit(EXIT_BAD_ARG);
	}
}

/* Use user-supplied XSL script to resolve CIR references. */
void undepend_cir_xsl(xmlDocPtr dm, xmlDocPtr cir, xsltStylesheetPtr style)
{
	xmlDocPtr res, muxdoc;
	xmlNodePtr mux;

	muxdoc = xmlNewDoc(BAD_CAST "1.0");
	mux = xmlNewNode(NULL, BAD_CAST "mux");
	xmlDocSetRootElement(muxdoc, mux);
	xmlAddChild(mux, xmlCopyNode(xmlDocGetRootElement(dm), 1));
	xmlAddChild(mux, xmlCopyNode(xmlDocGetRootElement(cir), 1));

	res = xsltApplyStylesheet(style, muxdoc, NULL);

	xmlDocSetRootElement(dm, xmlCopyNode(first_xpath_node(res, "/mux/dmodule[1]"), 1));
	xmlFreeDoc(res);
	xmlFreeDoc(muxdoc);
}

/* Apply the user-defined applicability to the CIR data module, then call the
 * appropriate function for the specific type of CIR. */
void undepend_cir(xmlDocPtr dm, const char *cirdocfname, bool add_src, const char *cir_xsl)
{
	xmlDocPtr cir;
	xmlXPathContextPtr ctxt;
	xmlXPathObjectPtr results;

	xmlNodePtr cirnode;
	xmlNodePtr content;
	xmlNodePtr referencedApplicGroup;

	char *cirtype;

	xmlDocPtr styledoc = NULL;

	cir = xmlReadFile(cirdocfname, NULL, 0);

	if (!cir) {
		fprintf(stderr, ERR_PREFIX "%s is not a valid CIR data module.\n", cirdocfname);
		exit(EXIT_BAD_XML);
	}

	ctxt = xmlXPathNewContext(cir);

	results = xmlXPathEvalExpression(BAD_CAST "//content", ctxt);
	content = results->nodesetval->nodeTab[0];
	xmlXPathFreeObject(results);

	results = xmlXPathEvalExpression(BAD_CAST "//referencedApplicGroup", ctxt);

	if (!xmlXPathNodeSetIsEmpty(results->nodesetval)) {
		referencedApplicGroup = results->nodesetval->nodeTab[0];
		strip_applic(referencedApplicGroup, content);
	}

	xmlXPathFreeObject(results);

	results = xmlXPathEvalExpression(BAD_CAST "//content/commonRepository/*[position()=last()]", ctxt);

	if (xmlXPathNodeSetIsEmpty(results->nodesetval)) {
		fprintf(stderr, ERR_PREFIX "%s is not a valid CIR data module.\n", cirdocfname);
		exit(EXIT_BAD_XML);
	}

	cirnode = results->nodesetval->nodeTab[0];
	xmlXPathFreeObject(results);

	cirtype = (char *) cirnode->name;

	if (cir_xsl) {
		styledoc = xmlReadFile(cir_xsl, NULL, 0);
	} else {
		unsigned char *xsl = NULL;
		unsigned int len = 0;

		if (!get_cir_xsl(cirtype, &xsl, &len)) {
			add_src = false;
		}

		styledoc = xmlReadMemory((const char *) xsl, len, NULL, NULL, 0);
	}

	if (styledoc) {
		xsltStylesheetPtr style;
		add_identity(styledoc);
		style = xsltParseStylesheetDoc(styledoc);
		undepend_cir_xsl(dm, cir, style);
		xsltFreeStylesheet(style);
	}

	xmlXPathFreeContext(ctxt);

	if (add_src) {
		xmlNodePtr security, dmIdent, repositorySourceDmIdent, cur;

		ctxt = xmlXPathNewContext(dm);
		results = xmlXPathEvalExpression(BAD_CAST "//dmStatus/security", ctxt);
		security = results->nodesetval->nodeTab[0];
		xmlXPathFreeObject(results);
		xmlXPathFreeContext(ctxt);

		repositorySourceDmIdent = xmlNewNode(NULL, BAD_CAST "repositorySourceDmIdent");
		repositorySourceDmIdent = xmlAddPrevSibling(security, repositorySourceDmIdent);

		ctxt = xmlXPathNewContext(cir);
		results = xmlXPathEvalExpression(BAD_CAST "//dmIdent", ctxt);
		dmIdent = results->nodesetval->nodeTab[0];
		xmlXPathFreeObject(results);
		xmlXPathFreeContext(ctxt);

		for (cur = dmIdent->children; cur; cur = cur->next) {
			xmlAddChild(repositorySourceDmIdent, xmlCopyNode(cur, 1));
		}
	}

	xmlFreeDoc(cir);
}

/* Set the issue and inwork numbers of the instance. */
void set_issue(xmlDocPtr dm, char *issinfo)
{
	char issue[4], inwork[3];
	xmlNodePtr issueInfo;

	if (sscanf(issinfo, "%3s-%2s", issue, inwork) != 2) {
		fprintf(stderr, ERR_PREFIX "Invalid format for issue/in-work number.\n");
		exit(EXIT_MISSING_ARGS);
	}

	issueInfo = first_xpath_node(dm, "//dmIdent/issueInfo|//pmIdent/issueInfo");

	xmlSetProp(issueInfo, BAD_CAST "issueNumber", BAD_CAST issue);
	xmlSetProp(issueInfo, BAD_CAST "inWork", BAD_CAST inwork);
}

/* Set the issue date of the instance. */
void set_issue_date(xmlDocPtr doc, const char *issdate)
{
	char year_s[5], month_s[3], day_s[3];
	xmlNodePtr issueDate;

	issueDate = first_xpath_node(doc, "//issueDate");

	if (sscanf(issdate, "%4s-%2s-%2s", year_s, month_s, day_s) != 3) {
		fprintf(stderr, ERR_PREFIX "Bad issue date: %s\n", issdate);
		exit(EXIT_BAD_DATE);
	}

	xmlSetProp(issueDate, BAD_CAST "year", BAD_CAST year_s);
	xmlSetProp(issueDate, BAD_CAST "month", BAD_CAST month_s);
	xmlSetProp(issueDate, BAD_CAST "day", BAD_CAST day_s);
}

/* Set the securty classification of the instance. */
void set_security(xmlDocPtr dm, char *sec)
{
	xmlNodePtr security = first_xpath_node(dm, "//dmStatus/security|//pmStatus/security");
	xmlSetProp(security, BAD_CAST "securityClassification", BAD_CAST sec);
}

/* Get the originator of the master. If it has no originator
 * (e.g. pub modules do not require one) then create one in the
 * instance.
 */
xmlNodePtr find_or_create_orig(xmlDocPtr doc)
{
	xmlNodePtr orig, rpc;
	orig = first_xpath_node(doc, "//originator");
	if (!orig) {
		orig = xmlNewNode(NULL, BAD_CAST "originator");
		rpc = first_xpath_node(doc, "//responsiblePartnerCompany");
		orig = xmlAddNextSibling(rpc, orig);
	}
	return orig;
}

/* Set the originator of the instance.
 *
 * When origspec == NULL, a default code and name are used to identify this
 * tool as the originator.
 *
 * Otherwise, origspec is a string in the form of "CODE/NAME", where CODE is
 * the NCAGE code and NAME is the enterprise name.
 */
void set_orig(xmlDocPtr doc, char *origspec)
{
	xmlNodePtr originator, enterpriseName;
	const char *code, *name;

	if (origspec) {
		code = strtok(origspec, "/");
		name = strtok(NULL, "");
	} else {
		code = DEFAULT_ORIG_CODE;
		name = DEFAULT_ORIG_NAME;
	}

	originator = find_or_create_orig(doc);

	if (code) {
		xmlSetProp(originator, BAD_CAST "enterpriseCode", BAD_CAST code);
	}

	if (name) {
		enterpriseName = find_child(originator, "enterpriseName");	
		if (enterpriseName) {
			xmlNodeSetContent(enterpriseName, BAD_CAST name);
		} else {
			enterpriseName = xmlNewChild(originator, NULL, BAD_CAST "enterpriseName", BAD_CAST name);
		}
	}
}

/* Determine if the whole data module is applicable. */
bool check_wholedm_applic(xmlDocPtr dm)
{
	xmlNodePtr applic;

	applic = first_xpath_node(dm, "//dmStatus/applic|//pmStatus/applic");

	return eval_applic_stmt(applic, true);
}

/* Read applicability definitions from the <assign> elements of a
 * product instance in the specified PCT data module.\
 */
void load_applic_from_pct(const char *pctfname, const char *product)
{
	xmlDocPtr pct;
	xmlXPathContextPtr ctx;
	xmlXPathObjectPtr obj;
	char xpath[256];

	pct = xmlReadFile(pctfname, NULL, 0);

	ctx = xmlXPathNewContext(pct);

	snprintf(xpath, 256, "//product[@id='%s']/assign", product);

	obj = xmlXPathEvalExpression(BAD_CAST xpath, ctx);

	if (xmlXPathNodeSetIsEmpty(obj->nodesetval)) {
		fprintf(stderr, ERR_PREFIX "No product '%s' in PCT '%s'.\n", product, pctfname);
		exit(EXIT_BAD_APPLIC);
	} else {
		int i;

		for (i = 0; i < obj->nodesetval->nodeNr; ++i) {
			char *ident, *type, *value;

			ident = (char *) xmlGetProp(obj->nodesetval->nodeTab[i],
				BAD_CAST "applicPropertyIdent");
			type  = (char *) xmlGetProp(obj->nodesetval->nodeTab[i],
				BAD_CAST "applicPropertyType");
			value = (char *) xmlGetProp(obj->nodesetval->nodeTab[i],
				BAD_CAST "applicPropertyValue");

			define_applic(ident, type, value);

			napplics++;

			xmlFree(ident);
			xmlFree(type);
			xmlFree(value);
		}
	}

	xmlXPathFreeObject(obj);
	xmlXPathFreeContext(ctx);
	xmlFreeDoc(pct);
}

/* Remove the extended identification from the instance. */
void strip_extension(xmlDocPtr doc)
{
	xmlNodePtr ext;

	ext = first_xpath_node(doc, "//identExtension");

	xmlUnlinkNode(ext);
	xmlFreeNode(ext);
}

void delete_xpath(xmlDocPtr doc, const char *xpath)
{

	xmlXPathContextPtr ctx;
	xmlXPathObjectPtr obj;

	ctx = xmlXPathNewContext(doc);
	obj = xmlXPathEvalExpression(BAD_CAST xpath, ctx);

	if (!xmlXPathNodeSetIsEmpty(obj->nodesetval)) {
		int i;
		for (i = 0; i < obj->nodesetval->nodeNr; ++i) {
			xmlUnlinkNode(obj->nodesetval->nodeTab[i]);
			xmlFreeNode(obj->nodesetval->nodeTab[i]);
		}
	}

	xmlXPathFreeObject(obj);
	xmlXPathFreeContext(ctx);
}

/* Removes invalid empty sections in a PM after all references have
 * been filtered out.
 */
void remove_emptry_pmentries(xmlDocPtr doc)
{
	delete_xpath(doc, "//pmEntry[not(dmRef|pmRef|externalPubRef)]");
}

/* General XSLT transformation with embedded stylesheet, preserving the DTD. */
void transform_doc(xmlDocPtr doc, unsigned char *xml, unsigned int len)
{
	xmlDocPtr styledoc, res, src;
	xsltStylesheetPtr style;
	xmlNodePtr old;

	styledoc = xmlReadMemory((const char *) xml, len, NULL, NULL, 0);
	add_identity(styledoc);
	style = xsltParseStylesheetDoc(styledoc);

	src = xmlCopyDoc(doc, 1);
	res = xsltApplyStylesheet(style, src, NULL);
	xmlFreeDoc(src);

	old = xmlDocSetRootElement(doc, xmlCopyNode(xmlDocGetRootElement(res), 1));
	xmlFreeNode(old);

	xmlFreeDoc(res);
	xsltFreeStylesheet(style);
}

void flatten_alts(xmlDocPtr doc)
{
	transform_doc(doc, xsl_flatten_alts_xsl, xsl_flatten_alts_xsl_len);
}

/* Print a usage message */
void show_help(void)
{
	printf("%.*s", help_msg_len, help_msg);
}

int main(int argc, char **argv)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	xmlNodePtr content;
	xmlNodePtr identAndStatusSection;
	xmlNodePtr referencedApplicGroup;
	xmlNodePtr comment;

	int i;
	int c;

	char src[PATH_MAX] = "";
	char code[256] = "";
	char out[PATH_MAX] = "-";
	bool clean = false;
	bool simpl = false;
	char tech[256] = "";
	char info[256] = "";
	bool autoname = false;
	char dir[PATH_MAX] = "";
	bool new_applic = false;
	char new_display_text[256] = "";
	char comment_text[256] = "";
	char extension[256] = "";
	char language[256] = "";
	bool add_source_ident = true;
	bool force_overwrite = false;
	bool use_stdin = false;
	char issinfo[8] = "";
	char secu[4] = "";
	bool wholedm = false;
	bool no_issue = false;
	char pctfname[PATH_MAX] = "";
	char product[64] = "";
	bool dmlist = false;
	FILE *list = stdin;
	char issdate[16] = "";
	bool stripext = false;
	bool verbose = false;
	bool setorig = false;
	char *origspec = NULL;
	bool flat_alts = false;

	int parseopts = 0;

	xmlNodePtr cirs, cir;

	bool ispm;

	exsltRegisterAll();

	opterr = 1;

	cirs = xmlNewNode(NULL, BAD_CAST "cirs");

	while ((c = getopt(argc, argv, "s:Se:Ec:o:O:faAt:i:Y:yC:l:R:r:n:u:wNP:p:LI:vx:gG:Fh?")) != -1) {
		switch (c) {
			case 's': strncpy(src, optarg, PATH_MAX - 1); break;
			case 'S': add_source_ident = false; break;
			case 'e': strncpy(extension, optarg, 255); break;
			case 'E': stripext = true; break;
			case 'c': strncpy(code, optarg, 255); break;
			case 'o': strncpy(out, optarg, PATH_MAX - 1); break;
			case 'O': autoname = true; strncpy(dir, optarg, PATH_MAX); break;
			case 'f': force_overwrite = true; break;
			case 'a': clean = true; break;
			case 'A': simpl = true; break;
			case 't': strncpy(tech, optarg, 255); break;
			case 'i': strncpy(info, optarg, 255); break;
			case 'Y': new_applic = true; strncpy(new_display_text, optarg, 255); break;
			case 'y': new_applic = true; break;
			case 'C': strncpy(comment_text, optarg, 255); break;
			case 'l': strncpy(language, optarg, 255); break;
			case 'R': xmlNewChild(cirs, NULL, BAD_CAST "cir", BAD_CAST optarg); break;
			case 'r': xmlSetProp(cirs->last, BAD_CAST "xsl", BAD_CAST optarg); break;
			case 'n': strncpy(issinfo, optarg, 6); break;
			case 'u': strncpy(secu, optarg, 2); break;
			case 'w': wholedm = true; break;
			case 'N': no_issue = true; break;
			case 'P': strncpy(pctfname, optarg, PATH_MAX - 1); break;
			case 'p': strncpy(product, optarg, 63); break;
			case 'L': dmlist = true; break;
			case 'I': strncpy(issdate, optarg, 15); break;
			case 'v': verbose = true; break;
			case 'x': dump_cir_xsl(optarg); exit(0);
			case 'g': setorig = true; break;
			case 'G': setorig = true; origspec = strdup(optarg); break;
			case 'F': flat_alts = true; break;
			case 'h':
			case '?':
				show_help();
				exit(EXIT_SUCCESS);
		}
	}


	if (strcmp(src, "") == 0) {
		strcpy(src, "-");
		use_stdin = true;
	} else if (dmlist) {
		list = fopen(src, "r");
	}

	if (!use_stdin && access(src, F_OK) == -1) {
		fprintf(stderr, ERR_PREFIX "Could not find source data module/list \"%s\".\n", src);
		exit(EXIT_MISSING_FILE);
	}

	if (autoname && strcmp(dir, "") == 0) {
		fprintf(stderr, ERR_PREFIX "No directory specified with -O.\n");
		exit(EXIT_MISSING_ARGS);
	}

	applicability = xmlNewNode(NULL, BAD_CAST "applic");

	if (strcmp(product, "") != 0) {
		if (strcmp(pctfname, "") == 0) {
			fprintf(stderr, ERR_PREFIX "No PCT specified (-P).\n");
			exit(EXIT_MISSING_ARGS);
		} else {
			if (access(pctfname, F_OK) == -1) {
				fprintf(stderr, ERR_PREFIX "PCT '%s' not found.\n", pctfname);
				exit(EXIT_MISSING_FILE);
			} else {
				load_applic_from_pct(pctfname, product);
			}
		}
	}

	/* All remaining arguments are treated as applic defs and copied to the
	 * global applicability list. */
	for (i = optind; i < argc; ++i) {
		char *ident, *type, *value;

		if (!strchr(argv[i], ':') || !strchr(argv[i], '=')) {
			fprintf(stderr, ERR_PREFIX "Malformed applicability definition: %s.\n", argv[i]);
			exit(EXIT_BAD_APPLIC);
		}

		ident = strtok(argv[i], ":");
		type  = strtok(NULL, "=");
		value = strtok(NULL, "");

		define_applic(ident, type, value);

		++napplics;
	}

	/* Bug in libxml < 20902 where entities in DTD are substituted even
	 * when XML_PARSE_NOENT is specified (default). Denying network access
	 * prevents it from substituting the %ISOEntities; parameter in the DTD
	 */
	if (LIBXML_VERSION < 20902) {
		parseopts |= XML_PARSE_NONET;
	}

	while (1) {
		if (dmlist) {
			if (!fgets(src, PATH_MAX - 1, list)) break;
			strtok(src, "\t\n");
		}

		doc = xmlReadFile(src, NULL, parseopts);

		if (!doc) {
			fprintf(stderr, ERR_PREFIX "%s does not contain valid XML.\n",
				use_stdin ? "stdin" : src);
			exit(EXIT_BAD_XML);
		}

		root = xmlDocGetRootElement(doc);

		ispm = xmlStrcmp(root->name, BAD_CAST "pm") == 0;

		content = find_req_child(root, "content");

		if (!wholedm || check_wholedm_applic(doc)) {
			if (add_source_ident) {
				add_source(doc);
			}

			for (cir = cirs->children; cir; cir = cir->next) {
				char *cirdocfname = (char *) xmlNodeGetContent(cir);
				char *cirxsl = (char *) xmlGetProp(cir, BAD_CAST "xsl");

				if (access(cirdocfname, F_OK) == -1) {
					fprintf(stderr, ERR_PREFIX "Could not find CIR %s.", cirdocfname);
					continue;
				}

				if (ispm) {
					undepend_cir(doc, cirdocfname, false, cirxsl);
				} else {
					undepend_cir(doc, cirdocfname, add_source_ident, cirxsl);
				}

				xmlFree(cirdocfname);
				xmlFree(cirxsl);
			}

			referencedApplicGroup = find_child(content, "referencedApplicGroup");

			if (referencedApplicGroup) {
				strip_applic(referencedApplicGroup, root);

				if (clean || simpl) {
					clean_applic(referencedApplicGroup, root);
				}

				if (simpl) {
					simpl_applic_clean(referencedApplicGroup);
				}
			}

			if (strcmp(extension, "") != 0) {
				set_extd(doc, extension);
			}

			if (stripext) {
				strip_extension(doc);
			}

			if (strcmp(code, "") != 0) {
				set_code(doc, code);
			}

			set_title(doc, tech, info);

			if (strcmp(language, "") != 0) {
				set_lang(doc, language);
			}

			if (new_applic && napplics > 0) {
				set_applic(doc, new_display_text);
			}

			if (strcmp(issinfo, "") != 0) {
				set_issue(doc, issinfo);
			}

			if (strcmp(issdate, "") != 0) {
				set_issue_date(doc, issdate);
			}

			if (strcmp(secu, "") != 0) {
				set_security(doc, secu);
			}

			if (setorig) {
				set_orig(doc, origspec);
			}

			if (strcmp(comment_text, "") != 0) {
				comment = xmlNewComment(BAD_CAST comment_text);
				identAndStatusSection = find_child(root, "identAndStatusSection");

				if (!identAndStatusSection) {
					fprintf(stderr, ERR_PREFIX "Data module missing child identAndStatusSection.");
					exit(EXIT_BAD_XML);
				}

				xmlAddPrevSibling(identAndStatusSection, comment);
			}

			if (ispm) {
				remove_emptry_pmentries(doc);
			}

			if (flat_alts) {
				flatten_alts(doc);
			}

			if (autoname) {
				auto_name(out, doc, dir, no_issue);

				if (access(out, F_OK) == 0 && !force_overwrite) {
					fprintf(stderr, ERR_PREFIX "%s already exists. Use -f to overwrite.\n", out);
					exit(EXIT_NO_OVERWRITE);
				}

				if (verbose) {
					puts(out);
				}
			}

			xmlSaveFile(out, doc);
		}

		xmlFreeDoc(doc);

		if (!dmlist) break;
	}

	if (list != stdin) fclose(list);

	free(origspec);
	xmlFreeNode(cirs);
	xmlFreeNode(applicability);
	xsltCleanupGlobals();
	xmlCleanupParser();

	return 0;
}
