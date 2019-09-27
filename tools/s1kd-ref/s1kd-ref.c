#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>
#include <ctype.h>
#include <libgen.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxslt/transform.h>
#include "s1kd_tools.h"
#include "xslt.h"

#define PROG_NAME "s1kd-ref"
#define VERSION "3.1.0"

#define ERR_PREFIX PROG_NAME ": ERROR: "
#define WRN_PREFIX PROG_NAME ": WARNING: "
#define INF_PREFIX PROG_NAME ": INFO: "

#define EXIT_MISSING_FILE 1
#define EXIT_BAD_INPUT 2
#define EXIT_BAD_ISSUE 3

#define OPT_TITLE (int) 0x01
#define OPT_ISSUE (int) 0x02
#define OPT_LANG  (int) 0x04
#define OPT_DATE  (int) 0x08
#define OPT_SRCID (int) 0x10
#define OPT_CIRID (int) 0x20
#define OPT_INS   (int) 0x40
#define OPT_URL   (int) 0x80

enum issue { ISS_20, ISS_21, ISS_22, ISS_23, ISS_30, ISS_40, ISS_41, ISS_42, ISS_50 };

#define DEFAULT_S1000D_ISSUE ISS_50

static enum verbosity { QUIET, NORMAL, VERBOSE } verbosity = NORMAL;

static xmlNode *find_child(xmlNode *parent, char *name)
{
	xmlNode *cur;

	if (!parent) return NULL;

	for (cur = parent->children; cur; cur = cur->next) {
		if (strcmp((char *) cur->name, name) == 0) {
			return cur;
		}
	}

	return NULL;
}

static xmlNodePtr first_xpath_node(xmlDocPtr doc, xmlNodePtr node, xmlChar *xpath)
{
	xmlXPathContextPtr ctx;
	xmlXPathObjectPtr obj;
	xmlNodePtr first;

	ctx = xmlXPathNewContext(doc ? doc : node->doc);
	ctx->node = node;
	obj = xmlXPathEvalExpression(xpath, ctx);

	first = xmlXPathNodeSetIsEmpty(obj->nodesetval) ? NULL : obj->nodesetval->nodeTab[0];

	xmlXPathFreeObject(obj);
	xmlXPathFreeContext(ctx);

	return first;
}

static xmlNodePtr find_or_create_refs(xmlDocPtr doc)
{
	xmlNodePtr refs;

	refs = first_xpath_node(doc, NULL, BAD_CAST "//content//refs");

	if (!refs) {
		xmlNodePtr content, child;
		content = first_xpath_node(doc, NULL, BAD_CAST "//content");
		child = xmlFirstElementChild(content);
		refs = xmlNewNode(NULL, BAD_CAST "refs");
		if (child) {
			refs = xmlAddPrevSibling(child, refs);
		} else {
			refs = xmlAddChild(content ,refs);
		}
	}

	return refs;
}

static void dump_node(xmlNodePtr node, const char *dst)
{
	xmlBufferPtr buf;
	buf = xmlBufferCreate();
	xmlNodeDump(buf, NULL, node, 0, 0);
	if (strcmp(dst, "-") == 0) {
		puts((char *) buf->content);
	} else {
		FILE *f;
		f = fopen(dst, "w");
		fputs((char *) buf->content, f);
		fclose(f);
	}
	xmlBufferFree(buf);
}

static xmlNodePtr new_issue_info(char *s)
{
	char n[4], w[3];
	xmlNodePtr issue_info;

	if (sscanf(s, "_%3[^-]-%2s", n, w) != 2) {
		return NULL;
	}

	issue_info = xmlNewNode(NULL, BAD_CAST "issueInfo");
	xmlSetProp(issue_info, BAD_CAST "issueNumber", BAD_CAST n);
	xmlSetProp(issue_info, BAD_CAST "inWork", BAD_CAST w);

	return issue_info;
}

static xmlNodePtr new_language(char *s)
{
	char l[4], c[3];
	xmlNodePtr language;

	if (sscanf(s, "_%3[^-]-%2s", l, c) != 2) {
		return NULL;
	}

	lowercase(l);

	language = xmlNewNode(NULL, BAD_CAST "language");
	xmlSetProp(language, BAD_CAST "languageIsoCode", BAD_CAST l);
	xmlSetProp(language, BAD_CAST "countryIsoCode", BAD_CAST c);

	return language;
}

static void set_xlink(xmlNodePtr node, const char *href)
{
	xmlNsPtr xlink;
	xlink = xmlNewNs(node, BAD_CAST "http://www.w3.org/1999/xlink", BAD_CAST "xlink");
	xmlSetNsProp(node, xlink, BAD_CAST "href", BAD_CAST href);
}

#define SME_FMT "SME-%255[^-]-%255[^-]-%14[^-]-%5s-%5s-%2s"
#define SMC_FMT "SMC-%14[^-]-%5s-%5s-%2s"

static xmlNodePtr new_smc_ref(const char *ref, const char *fname, int opts)
{
	char extension_producer[256] = "";
	char extension_code[256]     = "";
	char model_ident_code[15]    = "";
	char smc_issuer[6]            = "";
	char smc_number[6]            = "";
	char smc_volume[3]            = "";
	xmlNode *smc_ref;
	xmlNode *smc_ref_ident;
	xmlNode *smc_code;
	bool is_extended;
	int n;

	is_extended = strncmp(ref, "SME-", 4) == 0;

	if (is_extended) {
		n = sscanf(ref, SME_FMT,
			extension_producer,
			extension_code,
			model_ident_code,
			smc_issuer,
			smc_number,
			smc_volume);
		if (n != 6) {
			if (verbosity > QUIET) {
				fprintf(stderr, ERR_PREFIX "SCORM content package extended code invalid: %s\n", ref);
			}
			exit(EXIT_BAD_INPUT);
		}
	} else {
		n = sscanf(ref, SMC_FMT,
			model_ident_code,
			smc_issuer,
			smc_number,
			smc_volume);
		if (n != 4) {
			if (verbosity > QUIET) {
				fprintf(stderr, ERR_PREFIX "SCORM content package code invalid: %s\n", ref);
			}
			exit(EXIT_BAD_INPUT);
		}
	}

	smc_ref = xmlNewNode(NULL, BAD_CAST "scormContentPackageRef");
	smc_ref_ident = xmlNewChild(smc_ref, NULL, BAD_CAST "scormContentPackageRefIdent", NULL);

	if (is_extended) {
		xmlNode *ident_extension;
		ident_extension = xmlNewChild(smc_ref_ident, NULL, BAD_CAST "identExtension", NULL);
		xmlSetProp(ident_extension, BAD_CAST "extensionProducer", BAD_CAST extension_producer);
		xmlSetProp(ident_extension, BAD_CAST "extensionCode", BAD_CAST extension_code);
	}

	smc_code = xmlNewChild(smc_ref_ident, NULL, BAD_CAST "scormContentPackageCode", NULL);

	xmlSetProp(smc_code, BAD_CAST "modelIdentCode", BAD_CAST model_ident_code);
	xmlSetProp(smc_code, BAD_CAST "scormContentPackageIssuer", BAD_CAST smc_issuer);
	xmlSetProp(smc_code, BAD_CAST "scormContentPackageNumber", BAD_CAST smc_number);
	xmlSetProp(smc_code, BAD_CAST "scormContentPackageVolume", BAD_CAST smc_volume);

	if (opts) {
		xmlDocPtr doc;
		xmlNodePtr ref_smc_address = NULL;
		xmlNodePtr ref_smc_ident = NULL;
		xmlNodePtr ref_smc_address_items = NULL;
		xmlNodePtr ref_smc_title = NULL;
		xmlNodePtr ref_smc_issue_date = NULL;
		xmlNodePtr issue_info = NULL;
		xmlNodePtr language = NULL;
		char *s;

		if ((doc = read_xml_doc(fname))) {
			ref_smc_address = first_xpath_node(doc, NULL, BAD_CAST "//scormContentPackageAddress");
			ref_smc_ident = find_child(ref_smc_address, "scormContentPackageIdent");
			ref_smc_address_items = find_child(ref_smc_address, "scormContentPackageAddressItems");
			ref_smc_title = find_child(ref_smc_address_items, "scormContentPackageTitle");
			ref_smc_issue_date = find_child(ref_smc_address_items, "issueDate");
		}

		s = strchr(ref, '_');

		if (optset(opts, OPT_ISSUE)) {
			if (doc) {
				issue_info = xmlCopyNode(find_child(ref_smc_ident, "issueInfo"), 1);
			} else if (s && isdigit(s[1])) {
				issue_info = new_issue_info(s);
			} else {
				if (verbosity > QUIET) {
					fprintf(stderr, WRN_PREFIX "Could not read issue info from SCORM content package: %s\n", ref);
				}
				issue_info = NULL;
			}

			xmlAddChild(smc_ref_ident, issue_info);
		}

		if (optset(opts, OPT_LANG)) {
			if (doc) {
				language = xmlCopyNode(find_child(ref_smc_ident, "language"), 1);
			} else if (s && (s = strchr(s + 1, '_'))) {
				language = new_language(s);
			} else {
				if (verbosity > QUIET) {
					fprintf(stderr, WRN_PREFIX "Could not read language from SCORM content package: %s\n", ref);
				}
				language = NULL;
			}

			xmlAddChild(smc_ref_ident, language);
		}

		if (optset(opts, OPT_TITLE) || optset(opts, OPT_DATE)) {
			xmlNodePtr smc_ref_address_items = NULL, smc_title, issue_date;

			if (doc) {
				smc_ref_address_items = xmlNewChild(smc_ref, NULL, BAD_CAST "scormContentPackageRefAddressItems", NULL);
				smc_title = ref_smc_title;
				issue_date = ref_smc_issue_date;
			} else {
				smc_title = NULL;
				issue_date = NULL;
			}

			if (optset(opts, OPT_TITLE)) {
				if (smc_title) {
					xmlAddChild(smc_ref_address_items, xmlCopyNode(smc_title, 1));
				} else {
					if (verbosity > QUIET) {
						fprintf(stderr, WRN_PREFIX "Could not read title from SCORM content package: %s\n", ref);
					}
				}
			}
			if (optset(opts, OPT_DATE)) {
				if (issue_date) {
					xmlAddChild(smc_ref_address_items, xmlCopyNode(issue_date, 1));
				} else {
					if (verbosity > QUIET) {
						fprintf(stderr, WRN_PREFIX "Could not read date from SCORM content package: %s\n", ref);
					}
				}
			}
		}

		xmlFreeDoc(doc);

		if (optset(opts, OPT_SRCID)) {
			xmlNodePtr smc, issno, lang, src;

			smc   = xmlCopyNode(smc_code, 1);
			issno = xmlCopyNode(issue_info, 1);
			lang  = xmlCopyNode(language, 1);

			src = xmlNewNode(NULL, BAD_CAST "sourceScormContentPackageIdent");
			xmlAddChild(src, smc);
			xmlAddChild(src, lang);
			xmlAddChild(src, issno);

			xmlFreeNode(smc_ref);
			smc_ref = src;
		}

		if (optset(opts, OPT_URL)) {
			set_xlink(smc_ref, fname);
		}
	}

	return smc_ref;
}

#define PME_FMT "PME-%255[^-]-%255[^-]-%14[^-]-%5s-%5s-%2s"
#define PMC_FMT "PMC-%14[^-]-%5s-%5s-%2s"

static xmlNodePtr new_pm_ref(const char *ref, const char *fname, int opts)
{
	char extension_producer[256] = "";
	char extension_code[256]     = "";
	char model_ident_code[15]    = "";
	char pm_issuer[6]            = "";
	char pm_number[6]            = "";
	char pm_volume[3]            = "";
	xmlNode *pm_ref;
	xmlNode *pm_ref_ident;
	xmlNode *pm_code;
	bool is_extended;
	int n;

	is_extended = strncmp(ref, "PME-", 4) == 0;

	if (is_extended) {
		n = sscanf(ref, PME_FMT,
			extension_producer,
			extension_code,
			model_ident_code,
			pm_issuer,
			pm_number,
			pm_volume);
		if (n != 6) {
			if (verbosity > QUIET) {
				fprintf(stderr, ERR_PREFIX "Publication module extended code invalid: %s\n", ref);
			}
			exit(EXIT_BAD_INPUT);
		}
	} else {
		n = sscanf(ref, PMC_FMT,
			model_ident_code,
			pm_issuer,
			pm_number,
			pm_volume);
		if (n != 4) {
			if (verbosity > QUIET) {
				fprintf(stderr, ERR_PREFIX "Publication module code invalid: %s\n", ref);
			}
			exit(EXIT_BAD_INPUT);
		}
	}

	pm_ref = xmlNewNode(NULL, BAD_CAST "pmRef");
	pm_ref_ident = xmlNewChild(pm_ref, NULL, BAD_CAST "pmRefIdent", NULL);

	if (is_extended) {
		xmlNode *ident_extension;
		ident_extension = xmlNewChild(pm_ref_ident, NULL, BAD_CAST "identExtension", NULL);
		xmlSetProp(ident_extension, BAD_CAST "extensionProducer", BAD_CAST extension_producer);
		xmlSetProp(ident_extension, BAD_CAST "extensionCode", BAD_CAST extension_code);
	}

	pm_code = xmlNewChild(pm_ref_ident, NULL, BAD_CAST "pmCode", NULL);

	xmlSetProp(pm_code, BAD_CAST "modelIdentCode", BAD_CAST model_ident_code);
	xmlSetProp(pm_code, BAD_CAST "pmIssuer", BAD_CAST pm_issuer);
	xmlSetProp(pm_code, BAD_CAST "pmNumber", BAD_CAST pm_number);
	xmlSetProp(pm_code, BAD_CAST "pmVolume", BAD_CAST pm_volume);

	if (opts) {
		xmlDocPtr doc;
		xmlNodePtr ref_pm_address = NULL;
		xmlNodePtr ref_pm_ident = NULL;
		xmlNodePtr ref_pm_address_items = NULL;
		xmlNodePtr ref_pm_title = NULL;
		xmlNodePtr ref_pm_issue_date = NULL;
		xmlNodePtr issue_info = NULL;
		xmlNodePtr language = NULL;
		char *s;

		if ((doc = read_xml_doc(fname))) {
			ref_pm_address = first_xpath_node(doc, NULL, BAD_CAST "//pmAddress");
			ref_pm_ident = find_child(ref_pm_address, "pmIdent");
			ref_pm_address_items = find_child(ref_pm_address, "pmAddressItems");
			ref_pm_title = find_child(ref_pm_address_items, "pmTitle");
			ref_pm_issue_date = find_child(ref_pm_address_items, "issueDate");
		}

		s = strchr(ref, '_');

		if (optset(opts, OPT_ISSUE)) {
			if (doc) {
				issue_info = xmlCopyNode(find_child(ref_pm_ident, "issueInfo"), 1);
			} else if (s && isdigit(s[1])) {
				issue_info = new_issue_info(s);
			} else {
				if (verbosity > QUIET) {
					fprintf(stderr, WRN_PREFIX "Could not read issue info from publication module: %s\n", ref);
				}
				issue_info = NULL;
			}

			xmlAddChild(pm_ref_ident, issue_info);
		}

		if (optset(opts, OPT_LANG)) {
			if (doc) {
				language = xmlCopyNode(find_child(ref_pm_ident, "language"), 1);
			} else if (s && (s = strchr(s + 1, '_'))) {
				language = new_language(s);
			} else {
				if (verbosity > QUIET) {
					fprintf(stderr, WRN_PREFIX "Could not read language from publication module: %s\n", ref);
				}
				language = NULL;
			}

			xmlAddChild(pm_ref_ident, language);
		}

		if (optset(opts, OPT_TITLE) || optset(opts, OPT_DATE)) {
			xmlNodePtr pm_ref_address_items = NULL, pm_title, issue_date;

			if (doc) {
				pm_ref_address_items = xmlNewChild(pm_ref, NULL, BAD_CAST "pmRefAddressItems", NULL);
				pm_title = ref_pm_title;
				issue_date = ref_pm_issue_date;
			} else {
				pm_title = NULL;
				issue_date = NULL;
			}

			if (optset(opts, OPT_TITLE)) {
				if (pm_title) {
					xmlAddChild(pm_ref_address_items, xmlCopyNode(pm_title, 1));
				} else {
					if (verbosity > QUIET) {
						fprintf(stderr, WRN_PREFIX "Could not read title from publication module: %s\n", ref);
					}
				}
			}
			if (optset(opts, OPT_DATE)) {
				if (issue_date) {
					xmlAddChild(pm_ref_address_items, xmlCopyNode(issue_date, 1));
				} else {
					if (verbosity > QUIET) {
						fprintf(stderr, WRN_PREFIX "Could not read date from publication module: %s\n", ref);
					}
				}
			}
		}

		xmlFreeDoc(doc);

		if (optset(opts, OPT_SRCID)) {
			xmlNodePtr pmc, issno, lang, src;

			pmc   = xmlCopyNode(pm_code, 1);
			issno = xmlCopyNode(issue_info, 1);
			lang  = xmlCopyNode(language, 1);

			src = xmlNewNode(NULL, BAD_CAST "sourcePmIdent");
			xmlAddChild(src, pmc);
			xmlAddChild(src, lang);
			xmlAddChild(src, issno);

			xmlFreeNode(pm_ref);
			pm_ref = src;
		}

		if (optset(opts, OPT_URL)) {
			set_xlink(pm_ref, fname);
		}
	}

	return pm_ref;
}

#define DME_FMT "DME-%255[^-]-%255[^-]-%14[^-]-%4[^-]-%3[^-]-%1s%1s-%4[^-]-%2s%3[^-]-%3s%1s-%1s-%3s%1s"
#define DMC_FMT "DMC-%14[^-]-%4[^-]-%3[^-]-%1s%1s-%4[^-]-%2s%3[^-]-%3s%1s-%1s-%3s%1s"

static xmlNodePtr new_dm_ref(const char *ref, const char *fname, int opts)
{
	char extension_producer[256] = "";
	char extension_code[256]     = "";
	char model_ident_code[15]    = "";
	char system_diff_code[5]     = "";
	char system_code[4]          = "";
	char assy_code[5]            = "";
	char item_location_code[2]   = "";
	char learn_code[4]           = "";
	char learn_event_code[2]     = "";
	char sub_system_code[2]      = "";
	char sub_sub_system_code[2]  = "";
	char disassy_code[3]         = "";
	char disassy_code_variant[4] = "";
	char info_code[4]            = "";
	char info_code_variant[2]    = "";
	xmlNode *dm_ref;
	xmlNode *dm_ref_ident;
	xmlNode *dm_code;
	bool is_extended;
	bool has_learn;
	int n;

	is_extended = strncmp(ref, "DME-", 4) == 0;

	if (is_extended) {
		n = sscanf(ref, DME_FMT,
			extension_producer,
			extension_code,
			model_ident_code,
			system_diff_code,
			system_code,
			sub_system_code,
			sub_sub_system_code,
			assy_code,
			disassy_code,
			disassy_code_variant,
			info_code,
			info_code_variant,
			item_location_code,
			learn_code,
			learn_event_code);
		if (n != 15 && n != 13) {
			if (verbosity > QUIET) {
				fprintf(stderr, ERR_PREFIX "Data module extended code invalid: %s\n", ref);
			}
			exit(EXIT_BAD_INPUT);
		}
		has_learn = n == 15;
	} else {
		n = sscanf(ref, DMC_FMT,
			model_ident_code,
			system_diff_code,
			system_code,
			sub_system_code,
			sub_sub_system_code,
			assy_code,
			disassy_code,
			disassy_code_variant,
			info_code,
			info_code_variant,
			item_location_code,
			learn_code,
			learn_event_code);
		if (n != 13 && n != 11) {
			if (verbosity > QUIET) {
				fprintf(stderr, ERR_PREFIX "Data module code invalid: %s\n", ref);
			}
			exit(EXIT_BAD_INPUT);
		}
		has_learn = n == 13;
	}

	dm_ref = xmlNewNode(NULL, BAD_CAST "dmRef");
	dm_ref_ident = xmlNewChild(dm_ref, NULL, BAD_CAST "dmRefIdent", NULL);

	if (is_extended) {
		xmlNode *ident_extension;
		ident_extension = xmlNewChild(dm_ref_ident, NULL, BAD_CAST "identExtension", NULL);
		xmlSetProp(ident_extension, BAD_CAST "extensionProducer", BAD_CAST extension_producer);
		xmlSetProp(ident_extension, BAD_CAST "extensionCode", BAD_CAST extension_code);
	}

	dm_code = xmlNewChild(dm_ref_ident, NULL, BAD_CAST "dmCode", NULL);

	xmlSetProp(dm_code, BAD_CAST "modelIdentCode", BAD_CAST model_ident_code);
	xmlSetProp(dm_code, BAD_CAST "systemDiffCode", BAD_CAST system_diff_code);
	xmlSetProp(dm_code, BAD_CAST "systemCode", BAD_CAST system_code);
	xmlSetProp(dm_code, BAD_CAST "subSystemCode", BAD_CAST sub_system_code);
	xmlSetProp(dm_code, BAD_CAST "subSubSystemCode", BAD_CAST sub_sub_system_code);
	xmlSetProp(dm_code, BAD_CAST "assyCode", BAD_CAST assy_code);
	xmlSetProp(dm_code, BAD_CAST "disassyCode", BAD_CAST disassy_code);
	xmlSetProp(dm_code, BAD_CAST "disassyCodeVariant", BAD_CAST disassy_code_variant);
	xmlSetProp(dm_code, BAD_CAST "infoCode", BAD_CAST info_code);
	xmlSetProp(dm_code, BAD_CAST "infoCodeVariant", BAD_CAST info_code_variant);
	xmlSetProp(dm_code, BAD_CAST "itemLocationCode", BAD_CAST item_location_code);
	if (has_learn) {
		xmlSetProp(dm_code, BAD_CAST "learnCode", BAD_CAST learn_code);
		xmlSetProp(dm_code, BAD_CAST "learnEventCode", BAD_CAST learn_event_code);
	}

	if (opts) {
		xmlDocPtr doc;
		xmlNodePtr ref_dm_address = NULL;
		xmlNodePtr ref_dm_ident = NULL;
		xmlNodePtr ref_dm_address_items = NULL;
		xmlNodePtr ref_dm_title = NULL;
		xmlNodePtr ref_dm_issue_date = NULL;
		xmlNodePtr issue_info = NULL;
		xmlNodePtr language = NULL;
		char *s;

		if ((doc = read_xml_doc(fname))) {
			ref_dm_address = first_xpath_node(doc, NULL, BAD_CAST "//dmAddress");
			ref_dm_ident = find_child(ref_dm_address, "dmIdent");
			ref_dm_address_items = find_child(ref_dm_address, "dmAddressItems");
			ref_dm_title = find_child(ref_dm_address_items, "dmTitle");
			ref_dm_issue_date = find_child(ref_dm_address_items, "issueDate");
		}

		s = strchr(ref, '_');

		if (optset(opts, OPT_ISSUE)) {
			if (doc) {
				issue_info = xmlCopyNode(find_child(ref_dm_ident, "issueInfo"), 1);
			} else if (s && isdigit(s[1])) {
				issue_info = new_issue_info(s);
			} else {
				if (verbosity > QUIET) {
					fprintf(stderr, WRN_PREFIX "Could not read issue info from data module: %s\n", ref);
				}
				issue_info = NULL;
			}

			xmlAddChild(dm_ref_ident, issue_info);
		}

		if (optset(opts, OPT_LANG)) {
			if (doc) {
				language = xmlCopyNode(find_child(ref_dm_ident, "language"), 1);
			} else if (s && (s = strchr(s + 1, '_'))) {
				language = new_language(s);
			} else {
				if (verbosity > QUIET) {
					fprintf(stderr, WRN_PREFIX "Could not read language from data module: %s\n", ref);
				}
				language = NULL;
			}

			xmlAddChild(dm_ref_ident, language);
		}

		if (optset(opts, OPT_TITLE) || optset(opts, OPT_DATE)) {
			xmlNodePtr dm_ref_address_items = NULL, dm_title, issue_date;

			if (doc) {
				dm_ref_address_items = xmlNewChild(dm_ref, NULL, BAD_CAST "dmRefAddressItems", NULL);
				dm_title = xmlCopyNode(ref_dm_title, 1);
				issue_date = xmlCopyNode(ref_dm_issue_date, 1);
			} else {
				dm_title = NULL;
				issue_date = NULL;
			}

			if (optset(opts, OPT_TITLE)) {
				if (dm_title) {
					xmlAddChild(dm_ref_address_items, dm_title);
				} else {
					if (verbosity > QUIET) {
						fprintf(stderr, WRN_PREFIX "Could not read title from data module: %s\n", ref);
					}
				}
			}
			if (optset(opts, OPT_DATE)) {
				if (issue_date) {
					xmlAddChild(dm_ref_address_items, issue_date);
				} else {
					if (verbosity > QUIET) {
						fprintf(stderr, WRN_PREFIX "Could not read issue date from data module: %s\n", ref);
					}
				}
			}
		}

		xmlFreeDoc(doc);

		if (optset(opts, OPT_SRCID)) {
			xmlNodePtr dmc, issno, lang, src;

			dmc   = xmlCopyNode(dm_code, 1);
			issno = xmlCopyNode(issue_info, 1);
			lang  = xmlCopyNode(language, 1);

			src = xmlNewNode(NULL, BAD_CAST (optset(opts, OPT_CIRID) ? "repositorySourceDmIdent" : "sourceDmIdent"));
			xmlAddChild(src, dmc);
			xmlAddChild(src, lang);
			xmlAddChild(src, issno);

			xmlFreeNode(dm_ref);
			dm_ref = src;
		}

		if (optset(opts, OPT_URL)) {
			set_xlink(dm_ref, fname);
		}
	}

	return dm_ref;
}

#define COM_FMT "COM-%14[^-]-%5s-%4s-%5s-%1s"

static xmlNodePtr new_com_ref(const char *ref, const char *fname, int opts)
{
	char model_ident_code[15]  = "";
	char sender_ident[6]       = "";
	char year_of_data_issue[5] = "";
	char seq_number[6]         = "";
	char comment_type[2]       = "";

	int n;

	xmlNodePtr comment_ref, comment_ref_ident, comment_code;

	n = sscanf(ref, COM_FMT,
		model_ident_code,
		sender_ident,
		year_of_data_issue,
		seq_number,
		comment_type);
	if (n != 5) {
		if (verbosity > QUIET) {
			fprintf(stderr, ERR_PREFIX "Comment code invalid: %s\n", ref);
		}
		exit(EXIT_BAD_INPUT);
	}

	lowercase(comment_type);

	comment_ref = xmlNewNode(NULL, BAD_CAST "commentRef");
	comment_ref_ident = xmlNewChild(comment_ref, NULL, BAD_CAST "commentRefIdent", NULL);
	comment_code = xmlNewChild(comment_ref_ident, NULL, BAD_CAST "commentCode", NULL);

	xmlSetProp(comment_code, BAD_CAST "modelIdentCode", BAD_CAST model_ident_code);
	xmlSetProp(comment_code, BAD_CAST "senderIdent", BAD_CAST sender_ident);
	xmlSetProp(comment_code, BAD_CAST "yearOfDataIssue", BAD_CAST year_of_data_issue);
	xmlSetProp(comment_code, BAD_CAST "seqNumber", BAD_CAST seq_number);
	xmlSetProp(comment_code, BAD_CAST "commentType", BAD_CAST comment_type);

	if (opts) {
		xmlDocPtr doc;
		xmlNodePtr ref_comment_address;
		xmlNodePtr ref_comment_ident;
		char *s;

		if ((doc = read_xml_doc(fname))) {
			ref_comment_address = first_xpath_node(doc, NULL, BAD_CAST "//commentAddress");
			ref_comment_ident = find_child(ref_comment_address, "commentIdent");
		}

		s = strchr(ref, '_');

		if (optset(opts, OPT_LANG)) {
			xmlNodePtr language;

			if (doc) {
				language = xmlCopyNode(find_child(ref_comment_ident, "language"), 1);
			} else if (s && (s = strchr(s + 1, '_'))) {
				language = new_language(s);
			} else {
				if (verbosity > QUIET) {
					fprintf(stderr, WRN_PREFIX "Could not read language from comment: %s\n", ref);
				}
				language = NULL;
			}

			xmlAddChild(comment_ref_ident, language);
		}

		xmlFreeDoc(doc);

		if (optset(opts, OPT_URL)) {
			set_xlink(comment_ref, fname);
		}
	}

	return comment_ref;
}

#define DML_FMT "DML-%14[^-]-%5s-%1s-%4s-%5s"

static xmlNodePtr new_dml_ref(const char *ref, const char *fname, int opts)
{
	char model_ident_code[15]  = "";
	char sender_ident[6]       = "";
	char dml_type[2]           = "";
	char year_of_data_issue[5] = "";
	char seq_number[6]         = "";

	int n;

	xmlNodePtr dml_ref, dml_ref_ident, dml_code;

	n = sscanf(ref, DML_FMT,
		model_ident_code,
		sender_ident,
		dml_type,
		year_of_data_issue,
		seq_number);
	if (n != 5) {
		if (verbosity > QUIET) {
			fprintf(stderr, ERR_PREFIX "DML code invalid: %s\n", ref);
		}
		exit(EXIT_BAD_INPUT);
	}

	lowercase(dml_type);

	dml_ref = xmlNewNode(NULL, BAD_CAST "dmlRef");
	dml_ref_ident = xmlNewChild(dml_ref, NULL, BAD_CAST "dmlRefIdent", NULL);
	dml_code = xmlNewChild(dml_ref_ident, NULL, BAD_CAST "dmlCode", NULL);

	xmlSetProp(dml_code, BAD_CAST "modelIdentCode", BAD_CAST model_ident_code);
	xmlSetProp(dml_code, BAD_CAST "senderIdent", BAD_CAST sender_ident);
	xmlSetProp(dml_code, BAD_CAST "dmlType", BAD_CAST dml_type);
	xmlSetProp(dml_code, BAD_CAST "yearOfDataIssue", BAD_CAST year_of_data_issue);
	xmlSetProp(dml_code, BAD_CAST "seqNumber", BAD_CAST seq_number);

	if (opts) {
		xmlDocPtr doc;
		xmlNodePtr ref_dml_address;
		xmlNodePtr ref_dml_ident;
		char *s;

		if ((doc = read_xml_doc(fname))) {
			ref_dml_address = first_xpath_node(doc, NULL, BAD_CAST "//dmlAddress");
			ref_dml_ident = find_child(ref_dml_address, "dmlIdent");
		}

		s = strchr(ref, '_');

		if (optset(opts, OPT_ISSUE)) {
			xmlNodePtr issue_info;

			if (doc) {
				issue_info = xmlCopyNode(find_child(ref_dml_ident, "issueInfo"), 1);
			} else if (s && isdigit(s[1])) {
				issue_info = new_issue_info(s);
			} else {
				if (verbosity > QUIET) {
					fprintf(stderr, WRN_PREFIX "Could not read issue info from DML: %s\n", ref);
				}
				issue_info = NULL;
			}

			xmlAddChild(dml_ref_ident, issue_info);
		}

		xmlFreeDoc(doc);

		if (optset(opts, OPT_URL)) {
			set_xlink(dml_ref, fname);
		}
	}

	return dml_ref;
}

static xmlNodePtr new_icn_ref(const char *ref, const char *fname, int opts)
{
	xmlNodePtr info_entity_ref;

	info_entity_ref = xmlNewNode(NULL, BAD_CAST "infoEntityRef");

	xmlSetProp(info_entity_ref, BAD_CAST "infoEntityRefIdent", BAD_CAST ref);

	return info_entity_ref;
}

#define CSN_FMT "CSN-%14[^-]-%4[^-]-%3[^-]-%1s%1s-%4[^-]-%2s%3[^-]-%3s%1s-%1s"

static xmlNodePtr new_csn_ref(const char *ref, const char *fname, int opts)
{
	char model_ident_code[15]    = "";
	char system_diff_code[5]     = "";
	char system_code[4]          = "";
	char assy_code[5]            = "";
	char item_location_code[2]   = "";
	char sub_system_code[2]      = "";
	char sub_sub_system_code[2]  = "";
	char figure_number[3]         = "";
	char figure_number_variant[4] = "";
	char item[4]            = "";
	char item_variant[2]    = "";
	xmlNode *csn_ref;
	int n;

	n = sscanf(ref, CSN_FMT,
		model_ident_code,
		system_diff_code,
		system_code,
		sub_system_code,
		sub_sub_system_code,
		assy_code,
		figure_number,
		figure_number_variant,
		item,
		item_variant,
		item_location_code);
	if (n != 11) {
		if (verbosity > QUIET) {
			fprintf(stderr, ERR_PREFIX "CSN invalid: %s\n", ref);
		}
		exit(EXIT_BAD_INPUT);
	}

	csn_ref = xmlNewNode(NULL, BAD_CAST "catalogSeqNumberRef");

	xmlSetProp(csn_ref, BAD_CAST "modelIdentCode", BAD_CAST model_ident_code);
	xmlSetProp(csn_ref, BAD_CAST "systemDiffCode", BAD_CAST system_diff_code);
	xmlSetProp(csn_ref, BAD_CAST "systemCode", BAD_CAST system_code);
	xmlSetProp(csn_ref, BAD_CAST "subSystemCode", BAD_CAST sub_system_code);
	xmlSetProp(csn_ref, BAD_CAST "subSubSystemCode", BAD_CAST sub_sub_system_code);
	xmlSetProp(csn_ref, BAD_CAST "assyCode", BAD_CAST assy_code);
	xmlSetProp(csn_ref, BAD_CAST "figureNumber", BAD_CAST figure_number);
	xmlSetProp(csn_ref, BAD_CAST "figureNumberVariant", BAD_CAST figure_number_variant);
	xmlSetProp(csn_ref, BAD_CAST "item", BAD_CAST item);
	xmlSetProp(csn_ref, BAD_CAST "itemVariant", BAD_CAST item_variant);
	xmlSetProp(csn_ref, BAD_CAST "itemLocationCode", BAD_CAST item_location_code);

	if (optset(opts, OPT_URL)) {
		set_xlink(csn_ref, fname);
	}

	return csn_ref;
}

static bool is_smc_ref(const char *ref)
{
	return strncmp(ref, "SMC-", 4) == 0 || strncmp(ref, "SME-", 4) == 0;
}

static bool is_pm_ref(const char *ref)
{
	return strncmp(ref, "PMC-", 4) == 0 || strncmp(ref, "PME-", 4) == 0;
}

static bool is_dm_ref(const char *ref)
{
	return strncmp(ref, "DMC-", 4) == 0 || strncmp(ref, "DME-", 4) == 0;
}

static bool is_com_ref(const char *ref)
{
	return strncmp(ref, "COM-", 4) == 0;
}

static bool is_dml_ref(const char *ref)
{
	return strncmp(ref, "DML-", 4) == 0;
}

static bool is_icn_ref(const char *ref)
{
	return strncmp(ref, "ICN-", 4) == 0;
}

static bool is_csn_ref(const char *ref)
{
	return strncmp(ref, "CSN-", 4) == 0;
}

static void add_ref(const char *src, const char *dst, xmlNodePtr ref, int opts)
{
	xmlDocPtr doc;
	xmlNodePtr refs;

	if (!(doc = read_xml_doc(src))) {
		if (verbosity > QUIET) {
			fprintf(stderr, ERR_PREFIX "Could not read source data module: %s\n", src);
		}
		exit(EXIT_MISSING_FILE);
	}

	if (optset(opts, OPT_SRCID)) {
		xmlNodePtr src, node;

		src  = first_xpath_node(doc, NULL, BAD_CAST "//dmStatus/sourceDmIdent|//pmStatus/sourcePmIdent|//status/srcdmaddres");
		node = first_xpath_node(doc, NULL, BAD_CAST "(//dmStatus/repositorySourceDmIdent|//dmStatus/security|//pmStatus/security|//status/security)[1]");
		if (node) {
			if (src) {
				xmlUnlinkNode(src);
				xmlFreeNode(src);
			}
			xmlAddPrevSibling(node, xmlCopyNode(ref, 1));
		}
	} else {
		refs = find_or_create_refs(doc);
		xmlAddChild(refs, xmlCopyNode(ref, 1));
	}

	save_xml_doc(doc, dst);

	xmlFreeDoc(doc);
}

/* Apply a built-in XSLT transform to a doc in place. */
static void transform_doc(xmlDocPtr doc, unsigned char *xsl, unsigned int len)
{
	xmlDocPtr styledoc, src, res;
	xsltStylesheetPtr style;
	xmlNodePtr old;

	src = xmlCopyDoc(doc, 1);

	styledoc = read_xml_mem((const char *) xsl, len);
	style = xsltParseStylesheetDoc(styledoc);

	res = xsltApplyStylesheet(style, src, NULL);

	old = xmlDocSetRootElement(doc, xmlCopyNode(xmlDocGetRootElement(res), 1));
	xmlFreeNode(old);

	xmlFreeDoc(src);
	xmlFreeDoc(res);
	xsltFreeStylesheet(style);
}

static xmlNodePtr find_ext_pub(xmlDocPtr extpubs, const char *ref)
{
	xmlChar xpath[512];
	xmlNodePtr node;

	/* Attempt to match an exact code (e.g., "ABC") */
	xmlStrPrintf(xpath, 512, "//externalPubRef[externalPubRefIdent/externalPubCode='%s']", ref);
	node = first_xpath_node(extpubs, NULL, xpath);

	/* Attempt to match a file name (e.g., "ABC.PDF") */
	if (!node) {
		xmlStrPrintf(xpath, 512, "//externalPubRef[starts-with('%s', externalPubRefIdent/externalPubCode)]", ref);
		node = first_xpath_node(extpubs, NULL, xpath);
	}

	return xmlCopyNode(node, 1);
}

static xmlNodePtr new_ext_pub(const char *ref, const char *fname, int opts)
{
	xmlNodePtr epr, epr_ident;

	epr = xmlNewNode(NULL, BAD_CAST "externalPubRef");
	epr_ident = xmlNewChild(epr, NULL, BAD_CAST "externalPubRefIdent", NULL);

	if (optset(opts, OPT_TITLE)) {
		xmlNewTextChild(epr_ident, NULL, BAD_CAST "externalPubTitle", BAD_CAST ref);
	} else {
		xmlNewTextChild(epr_ident, NULL, BAD_CAST "externalPubCode", BAD_CAST ref);
	}

	if (optset(opts, OPT_URL)) {
		set_xlink(epr, fname);
	}

	return epr;
}

static xmlNodePtr find_ref_type(const char *fname, int opts)
{
	xmlDocPtr doc, styledoc, res;
	xsltStylesheetPtr style;
	xmlNodePtr node = NULL;

	if (!(doc = read_xml_doc(fname))) {
		return NULL;
	}

	styledoc = read_xml_mem((const char *) ref_xsl, ref_xsl_len);
	style = xsltParseStylesheetDoc(styledoc);

	res = xsltApplyStylesheet(style, doc, NULL);

	if (res->children) {
		const char *ref;
		xmlNodePtr (*f)(const char *, const char *, int) = NULL;

		ref = (char *) res->children->content;

		if (is_dm_ref(ref)) {
			f = new_dm_ref;
		} else if (is_pm_ref(ref)) {
			f = new_pm_ref;
		} else if (is_smc_ref(ref)) {
			f = new_smc_ref;
		} else if (is_com_ref(ref)) {
			f = new_com_ref;
		} else if (is_dml_ref(ref)) {
			f = new_dml_ref;
		} else if (is_icn_ref(ref)) {
			f = new_icn_ref;
		}

		if (f) {
			node = f(ref, fname, opts);
		}
	}

	xmlFreeDoc(res);
	xsltFreeStylesheet(style);

	xmlFreeDoc(doc);

	return node;
}

static void print_ref(const char *src, const char *dst, const char *ref,
	const char *fname, int opts, bool overwrite, enum issue iss,
	xmlDocPtr extpubs)
{
	xmlNodePtr node;
	xmlNodePtr (*f)(const char *, const char *, int);

	if (is_dm_ref(ref)) {
		f = new_dm_ref;
	} else if (is_pm_ref(ref)) {
		f = new_pm_ref;
	} else if (is_smc_ref(ref)) {
		f = new_smc_ref;
	} else if (is_com_ref(ref)) {
		f = new_com_ref;
	} else if (is_dml_ref(ref)) {
		f = new_dml_ref;
	} else if (is_icn_ref(ref)) {
		f = new_icn_ref;
	} else if (is_csn_ref(ref)) {
		f = new_csn_ref;
	} else if (extpubs && (node = find_ext_pub(extpubs, ref))) {
		f = NULL;
	} else if ((node = find_ref_type(fname, opts))) {
		f = NULL;
	} else {
		f = new_ext_pub;
	}

	if (f) {
		node = f(ref, fname, opts);
	}

	if (iss < DEFAULT_S1000D_ISSUE) {
		unsigned char *xsl;
		unsigned int len;
		xmlDocPtr doc;

		switch (iss) {
			case ISS_20:
				xsl = ___common_to20_xsl;
				len = ___common_to20_xsl_len;
				break;
			case ISS_21:
				xsl = ___common_to21_xsl;
				len = ___common_to21_xsl_len;
				break;
			case ISS_22:
				xsl = ___common_to22_xsl;
				len = ___common_to22_xsl_len;
				break;
			case ISS_23:
				xsl = ___common_to23_xsl;
				len = ___common_to23_xsl_len;
				break;
			case ISS_30:
				xsl = ___common_to30_xsl;
				len = ___common_to30_xsl_len;
				break;
			case ISS_40:
				xsl = ___common_to40_xsl;
				len = ___common_to40_xsl_len;
				break;
			case ISS_41:
				xsl = ___common_to41_xsl;
				len = ___common_to41_xsl_len;
				break;
			case ISS_42:
				xsl = ___common_to42_xsl;
				len = ___common_to42_xsl_len;
				break;
			default:
				xsl = NULL;
				len = 0;
				break;
		}

		doc = xmlNewDoc(BAD_CAST "1.0");
		xmlDocSetRootElement(doc, node);

		transform_doc(doc, xsl, len);

		node = xmlCopyNode(xmlDocGetRootElement(doc), 1);

		xmlFreeDoc(doc);
	}

	if (optset(opts, OPT_INS)) {
		if (verbosity >= VERBOSE) {
			fprintf(stderr, INF_PREFIX "Adding reference %s to %s...\n", ref, src);
		}

		if (overwrite) {
			add_ref(src, src, node, opts);
		} else {
			add_ref(src, dst, node, opts);
		}
	} else {
		dump_node(node, dst);
	}

	xmlFreeNode(node);
}

static char *trim(char *str)
{
	char *end;

	while (isspace(*str)) str++;

	if (*str == 0) return str;

	end = str + strlen(str) - 1;
	while (end > str && isspace(*end)) end--;

	*(end + 1) = 0;

	return str;
}

static enum issue spec_issue(const char *s)
{
	if (strcmp(s, "2.0") == 0) {
		return ISS_20;
	} else if (strcmp(s, "2.1") == 0) {
		return ISS_21;
	} else if (strcmp(s, "2.2") == 0) {
		return ISS_22;
	} else if (strcmp(s, "2.3") == 0) {
		return ISS_23;
	} else if (strcmp(s, "3.0") == 0) {
		return ISS_30;
	} else if (strcmp(s, "4.0") == 0) {
		return ISS_40;
	} else if (strcmp(s, "4.1") == 0) {
		return ISS_41;
	} else if (strcmp(s, "4.2") == 0) {
		return ISS_42;
	} else if (strcmp(s, "5.0") == 0) {
		return ISS_50;
	}

	if (verbosity > QUIET) {
		fprintf(stderr, ERR_PREFIX "Unsupported issue: %s\n", s);
	}
	exit(EXIT_BAD_ISSUE);
}

static void show_help(void)
{
	puts("Usage: " PROG_NAME " [-dfilqRrStuvh?] [-$ <issue>] [-s <src>] [-o <dst>] [-3 <file>] [<code>|<file> ...]");
	puts("");
	puts("Options:");
	puts("  -$, --issue <issue>        Output XML for the specified issue of S1000D.");
	puts("  -d, --include-date         Include issue date (target must be file)");
	puts("  -f, --overwrite            Overwrite source data module instead of writing to stdout.");
	puts("  -h, -?, --help             Show this help message.");
	puts("  -i, --include-issue        Include issue info.");
	puts("  -l, --include-lang         Include language.");
	puts("  -o, --out <dst>            Output to <dst> instead of stdout.");
	puts("  -q, --quiet                Quiet mode. Do not print errors.");
	puts("  -R, --repository-id        Generate a <repositorySourceDmIdent>.");
	puts("  -r, --add                  Add reference to data module's <refs> table.");
	puts("  -S, --source-id            Generate a <sourceDmIdent> or <sourcePmIdent>.");
	puts("  -s, --source <src>         Source data module to add references to.");
	puts("  -t, --include-title        Include title (target must be file)");
	puts("  -u, --include-url          Include xlink:href to the full URL/filename.");
	puts("  -v, --verbose              Verbose output.");
	puts("  -3, --externalpubs <file>  Use a custom .externalpubs file.");
	puts("  --version                  Show version information.");
	puts("  <code>                     The code of the reference (must include prefix DMC/PMC/etc.).");
	puts("  <file>                     A file to reference.");
	LIBXML2_PARSE_LONGOPT_HELP
}

static void show_version(void)
{
	printf("%s (s1kd-tools) %s\n", PROG_NAME, VERSION);
	printf("Using libxml %s and libxslt %s\n", xmlParserVersion, xsltEngineVersion);
}

int main(int argc, char **argv)
{
	char scratch[PATH_MAX];
	int i;
	int opts = 0;
	char src[PATH_MAX] = "-";
	char dst[PATH_MAX] = "-";
	bool overwrite = false;
	enum issue iss = DEFAULT_S1000D_ISSUE;
	char extpubs_fname[PATH_MAX] = "";
	xmlDocPtr extpubs = NULL;

	const char *sopts = "3:filo:qRrSs:tvd$:uh?";
	struct option lopts[] = {
		{"version"      , no_argument      , 0, 0},
		{"help"         , no_argument      , 0, 'h'},
		{"externalpubs" , required_argument, 0, '3'},
		{"overwrite"    , no_argument      , 0, 'f'},
		{"include-issue", no_argument      , 0, 'i'},
		{"include-lang" , no_argument      , 0, 'l'},
		{"out"          , required_argument, 0, 'o'},
		{"quiet"        , no_argument      , 0, 'q'},
		{"add"          , no_argument      , 0, 'r'},
		{"repository-id", no_argument      , 0, 'R'},
		{"source-id"    , no_argument      , 0, 'S'},
		{"source"       , required_argument, 0, 's'},
		{"include-title", no_argument      , 0, 't'},
		{"verbose"      , no_argument      , 0, 'v'},
		{"include-date" , no_argument      , 0, 'd'},
		{"issue"        , required_argument, 0, '$'},
		{"include-url"  , no_argument      , 0, 'u'},
		LIBXML2_PARSE_LONGOPT_DEFS
		{0, 0, 0, 0}
	};
	int loptind = 0;

	while ((i = getopt_long(argc, argv, sopts, lopts, &loptind)) != -1) {
		switch (i) {
			case 0:
				if (strcmp(lopts[loptind].name, "version") == 0) {
					show_version();
					return EXIT_SUCCESS;
				}
				LIBXML2_PARSE_LONGOPT_HANDLE(lopts, loptind)
				break;
			case '3': strncpy(extpubs_fname, optarg, PATH_MAX - 1); break;
			case 'f': overwrite = true; break;
			case 'i': opts |= OPT_ISSUE; break;
			case 'l': opts |= OPT_LANG; break;
			case 'o': strcpy(dst, optarg); break;
			case 'q': verbosity = QUIET; break;
			case 'r': opts |= OPT_INS; break;
			case 'R': opts |= OPT_CIRID;
			case 'S': opts |= OPT_SRCID; opts |= OPT_ISSUE; opts |= OPT_LANG; break;
			case 's': strcpy(src, optarg); break;
			case 't': opts |= OPT_TITLE; break;
			case 'v': verbosity = VERBOSE; break;
			case 'd': opts |= OPT_DATE; break;
			case '$': iss = spec_issue(optarg); break;
			case 'u': opts |= OPT_URL; break;
			case '?':
			case 'h': show_help(); return EXIT_SUCCESS;
		}
	}

	/* Load .externalpubs config file. */
	if (strcmp(extpubs_fname, "") != 0 || find_config(extpubs_fname, DEFAULT_EXTPUBS_FNAME)) {
		extpubs = read_xml_doc(extpubs_fname);
	}

	if (optind < argc) {
		for (i = optind; i < argc; ++i) {
			char *base;

			if (strncmp(argv[i], "URN:S1000D:", 11) == 0) {
				base = argv[i] + 11;
			} else {
				strcpy(scratch, argv[i]);
				base = basename(scratch);
			}

			print_ref(src, dst, base, argv[i], opts, overwrite, iss, extpubs);
		}
	} else {
		while (fgets(scratch, PATH_MAX, stdin)) {
			print_ref(src, dst, trim(scratch), NULL, opts, overwrite, iss, extpubs);
		}
	}

	xmlFreeDoc(extpubs);
	xsltCleanupGlobals();
	xmlCleanupParser();

	return 0;
}
