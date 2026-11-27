package com.brenoworker.windroid;

import org.json.JSONArray;
import org.json.JSONObject;

import java.net.URI;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class DeclarativeRules {

    static final int MAIN_FRAME = 1;
    static final int SUB_FRAME = 1 << 1;
    static final int STYLESHEET = 1 << 2;
    static final int SCRIPT = 1 << 3;
    static final int IMAGE = 1 << 4;
    static final int FONT = 1 << 5;
    static final int OBJECT = 1 << 6;
    static final int XHR = 1 << 7;
    static final int PING = 1 << 8;
    static final int CSP_REPORT = 1 << 9;
    static final int MEDIA = 1 << 10;
    static final int WEBSOCKET = 1 << 11;
    static final int OTHER = 1 << 12;
    private static final int EVERY = (1 << 13) - 1;

    static final int REDIRECT = 1;
    static final int UPGRADE = 2;
    static final int BLOCK = 3;
    static final int ALLOW_ALL = 4;
    static final int ALLOW = 5;

    private static final String[] TYPE_NAMES = {"main_frame", "sub_frame", "stylesheet", "script", "image", "font", "object",
            "xmlhttprequest", "ping", "csp_report", "media", "websocket", "other"};
    private static final Set<String> COMMON = new HashSet<>();
    private static final Set<String> TWO_LEVEL = new HashSet<>();

    static {
        for (String s : new String[]{"http", "https", "www", "com", "net", "org", "html", "php"}) COMMON.add(s);
        for (String s : new String[]{"co.uk", "org.uk", "gov.uk", "ac.uk", "com.br", "net.br", "org.br", "gov.br", "com.au", "net.au",
                "org.au", "co.jp", "ne.jp", "or.jp", "co.kr", "co.in", "co.nz", "co.za", "com.ar", "com.mx", "com.cn", "net.cn",
                "com.tr", "com.tw", "com.hk", "com.sg", "com.co", "com.pe", "com.ua", "co.id", "co.il", "github.io"}) {
            TWO_LEVEL.add(s);
        }
    }

    private DeclarativeRules() {
    }

    static final class Rule {
        int id;
        int priority = 1;
        int action;
        int types = EVERY & ~MAIN_FRAME;
        Set<String> methods;
        Set<String> excludedMethods;
        Set<String> requestDomains;
        Set<String> excludedRequestDomains;
        Set<String> initiatorDomains;
        Set<String> excludedInitiatorDomains;
        Set<Integer> tabIds;
        Set<Integer> excludedTabIds;
        int domainType;
        String urlFilter;
        String regexFilter;
        boolean caseSensitive;
        String redirectUrl;
        String extensionPath;
        String regexSubstitution;
        JSONObject transform;
        private volatile Pattern pattern;
        private volatile boolean broken;

        private Pattern pattern() {
            Pattern p = pattern;
            if (p != null || broken) return p;
            try {
                String re = regexFilter != null ? regexFilter : filterToRegex(urlFilter);
                p = Pattern.compile(re, caseSensitive ? 0 : Pattern.CASE_INSENSITIVE);
                pattern = p;
            } catch (RuntimeException e) {
                broken = true;
            }
            return p;
        }

        boolean matches(Request r) {
            if ((types & r.types) == 0) return false;
            if (methods != null && !methods.contains(r.method)) return false;
            if (excludedMethods != null && excludedMethods.contains(r.method)) return false;
            if (tabIds != null && !tabIds.contains(r.tab)) return false;
            if (excludedTabIds != null && excludedTabIds.contains(r.tab)) return false;
            if (requestDomains != null && !domainIn(requestDomains, r.host)) return false;
            if (excludedRequestDomains != null && domainIn(excludedRequestDomains, r.host)) return false;
            if (initiatorDomains != null && !domainIn(initiatorDomains, r.initiator)) return false;
            if (excludedInitiatorDomains != null && domainIn(excludedInitiatorDomains, r.initiator)) return false;
            if (domainType != 0) {
                boolean third = !r.initiator.isEmpty() && !site(r.initiator).equals(site(r.host));
                if (third != (domainType == 2)) return false;
            }
            if (urlFilter == null && regexFilter == null) return true;
            Pattern p = pattern();
            return p != null && p.matcher(r.url).find();
        }
    }

    static final class Request {
        final String url;
        final String host;
        final String initiator;
        final int types;
        final String method;
        final int tab;

        Request(String url, String initiatorUrl, int types, String method, int tab) {
            this.url = url;
            this.host = hostOf(url);
            this.initiator = hostOf(initiatorUrl);
            this.types = types;
            this.method = method == null ? "get" : method.toLowerCase(Locale.ROOT);
            this.tab = tab;
        }
    }

    static final class Index {
        private final Map<String, List<Rule>> byToken = new HashMap<>();
        private final Map<String, List<Rule>> byDomain = new HashMap<>();
        private final List<Rule> generic = new ArrayList<>();
        int size;

        void add(Rule r) {
            size++;
            String token = r.regexFilter == null ? token(r.urlFilter) : null;
            if (token != null && !COMMON.contains(token)) {
                bucket(byToken, token).add(r);
            } else if (r.requestDomains != null) {
                for (String d : r.requestDomains) bucket(byDomain, d).add(r);
            } else if (token != null) {
                bucket(byToken, token).add(r);
            } else {
                generic.add(r);
            }
        }

        private static List<Rule> bucket(Map<String, List<Rule>> map, String key) {
            List<Rule> list = map.get(key);
            if (list == null) {
                list = new ArrayList<>(1);
                map.put(key, list);
            }
            return list;
        }

        Rule best(Request r, int onlyAction) {
            Rule best = scan(generic, r, onlyAction, null);
            String url = r.url.toLowerCase(Locale.ROOT);
            int start = -1;
            for (int i = 0; i <= url.length(); i++) {
                boolean part = i < url.length() && tokenChar(url.charAt(i));
                if (part && start < 0) {
                    start = i;
                } else if (!part && start >= 0) {
                    List<Rule> list = byToken.get(url.substring(start, i));
                    if (list != null) best = scan(list, r, onlyAction, best);
                    start = -1;
                }
            }
            String host = r.host;
            while (!host.isEmpty() && !byDomain.isEmpty()) {
                List<Rule> list = byDomain.get(host);
                if (list != null) best = scan(list, r, onlyAction, best);
                int dot = host.indexOf('.');
                host = dot < 0 ? "" : host.substring(dot + 1);
            }
            return best;
        }

        private static Rule scan(List<Rule> list, Request r, int onlyAction, Rule best) {
            for (Rule rule : list) {
                if (onlyAction != 0 && rule.action != onlyAction) continue;
                if (best != null && (rule.priority < best.priority || (rule.priority == best.priority && rule.action <= best.action))) {
                    continue;
                }
                if (rule.matches(r)) best = rule;
            }
            return best;
        }
    }

    private static boolean tokenChar(char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '%';
    }

    static String token(String filter) {
        if (filter == null) return null;
        String f = filter.toLowerCase(Locale.ROOT);
        String best = null;
        boolean bestCommon = true;
        int i = 0;
        while (i < f.length()) {
            if (!tokenChar(f.charAt(i))) {
                i++;
                continue;
            }
            int j = i;
            while (j < f.length() && tokenChar(f.charAt(j))) j++;
            boolean left = i > 0 && f.charAt(i - 1) != '*';
            boolean right = j < f.length() && f.charAt(j) != '*';
            if (left && right && j - i >= 2) {
                String t = f.substring(i, j);
                boolean common = COMMON.contains(t);
                if (best == null || (bestCommon && !common) || (bestCommon == common && t.length() > best.length())) {
                    best = t;
                    bestCommon = common;
                }
            }
            i = j;
        }
        return best;
    }

    static String filterToRegex(String filter) {
        StringBuilder sb = new StringBuilder();
        int start = 0;
        int end = filter.length();
        if (filter.startsWith("||")) {
            sb.append("^[a-z][a-z0-9+.-]*://(?:[^/?#]*\\.)?");
            start = 2;
        } else if (filter.startsWith("|")) {
            sb.append('^');
            start = 1;
        }
        boolean anchorEnd = end > start && filter.charAt(end - 1) == '|';
        if (anchorEnd) end--;
        for (int i = start; i < end; i++) {
            char c = filter.charAt(i);
            if (c == '*') {
                sb.append(".*");
            } else if (c == '^') {
                sb.append("(?:[^a-zA-Z0-9_.%-]|$)");
            } else {
                if ("\\.[]{}()+-=!?$|<>".indexOf(c) >= 0) sb.append('\\');
                sb.append(c);
            }
        }
        if (anchorEnd) sb.append('$');
        return sb.toString();
    }

    static String hostOf(String url) {
        if (url == null) return "";
        int s = url.indexOf("://");
        if (s < 0) return "";
        int from = s + 3;
        int end = from;
        while (end < url.length() && "/?#".indexOf(url.charAt(end)) < 0) end++;
        String authority = url.substring(from, end);
        int at = authority.lastIndexOf('@');
        if (at >= 0) authority = authority.substring(at + 1);
        if (authority.startsWith("[")) {
            int close = authority.indexOf(']');
            return close < 0 ? authority : authority.substring(0, close + 1).toLowerCase(Locale.ROOT);
        }
        int colon = authority.indexOf(':');
        if (colon >= 0) authority = authority.substring(0, colon);
        return authority.toLowerCase(Locale.ROOT);
    }

    static String site(String host) {
        String[] p = host.split("\\.");
        int n = p.length;
        if (n <= 2 || host.matches("[0-9.]+")) return host;
        String two = p[n - 2] + "." + p[n - 1];
        return TWO_LEVEL.contains(two) ? p[n - 3] + "." + two : two;
    }

    private static boolean domainIn(Set<String> set, String host) {
        while (!host.isEmpty()) {
            if (set.contains(host)) return true;
            int dot = host.indexOf('.');
            host = dot < 0 ? "" : host.substring(dot + 1);
        }
        return false;
    }

    static int typeBit(String name) {
        for (int i = 0; i < TYPE_NAMES.length; i++) if (TYPE_NAMES[i].equals(name)) return 1 << i;
        return 0;
    }

    static int guessTypes(String url, boolean mainFrame, String accept) {
        if (mainFrame) return MAIN_FRAME;
        String path = url;
        int q = path.indexOf('?');
        if (q >= 0) path = path.substring(0, q);
        int h = path.indexOf('#');
        if (h >= 0) path = path.substring(0, h);
        int slash = path.lastIndexOf('/');
        int dot = path.lastIndexOf('.');
        String ext = dot > slash ? path.substring(dot + 1).toLowerCase(Locale.ROOT) : "";
        String a = accept == null ? "" : accept.toLowerCase(Locale.ROOT);
        if (a.startsWith("text/css")) return STYLESHEET;
        if (a.startsWith("image/")) return IMAGE;
        if (a.startsWith("text/html")) return SUB_FRAME;
        switch (ext) {
            case "js":
            case "mjs":
                return SCRIPT | XHR;
            case "css":
                return STYLESHEET;
            case "png":
            case "jpg":
            case "jpeg":
            case "gif":
            case "webp":
            case "avif":
            case "svg":
            case "ico":
            case "bmp":
                return IMAGE | XHR;
            case "woff":
            case "woff2":
            case "ttf":
            case "otf":
            case "eot":
                return FONT;
            case "mp4":
            case "webm":
            case "m4a":
            case "mp3":
            case "ogg":
            case "m3u8":
                return MEDIA | XHR;
            case "swf":
                return OBJECT;
            default:
                return SCRIPT | XHR | PING | OTHER;
        }
    }

    static Rule parse(JSONObject o) {
        if (o == null) throw new IllegalArgumentException("Rule must be an object");
        Rule r = new Rule();
        r.id = o.optInt("id", 0);
        if (r.id < 1) throw new IllegalArgumentException("Rule has an invalid id");
        r.priority = Math.max(1, o.optInt("priority", 1));
        JSONObject action = o.optJSONObject("action");
        JSONObject c = o.optJSONObject("condition");
        if (action == null || c == null) throw new IllegalArgumentException("Rule " + r.id + " needs an action and a condition");
        String type = action.optString("type");
        switch (type) {
            case "block":
                r.action = BLOCK;
                break;
            case "allow":
                r.action = ALLOW;
                break;
            case "allowAllRequests":
                r.action = ALLOW_ALL;
                break;
            case "upgradeScheme":
                r.action = UPGRADE;
                break;
            case "redirect": {
                r.action = REDIRECT;
                JSONObject rd = action.optJSONObject("redirect");
                if (rd == null) throw new IllegalArgumentException("Rule " + r.id + " has no redirect");
                r.redirectUrl = str(rd, "url");
                r.extensionPath = str(rd, "extensionPath");
                r.regexSubstitution = str(rd, "regexSubstitution");
                r.transform = rd.optJSONObject("transform");
                break;
            }
            case "modifyHeaders":
                return null;
            default:
                throw new IllegalArgumentException("Rule " + r.id + " has an unknown action type");
        }
        r.urlFilter = str(c, "urlFilter");
        r.regexFilter = str(c, "regexFilter");
        if (r.urlFilter != null && r.regexFilter != null) {
            throw new IllegalArgumentException("Rule " + r.id + " can't have both urlFilter and regexFilter");
        }
        r.caseSensitive = c.optBoolean("isUrlFilterCaseSensitive", false);
        JSONArray types = c.optJSONArray("resourceTypes");
        JSONArray excluded = c.optJSONArray("excludedResourceTypes");
        if (types != null) {
            r.types = 0;
            for (int i = 0; i < types.length(); i++) r.types |= typeBit(types.optString(i));
        } else if (excluded != null) {
            r.types = EVERY;
            for (int i = 0; i < excluded.length(); i++) r.types &= ~typeBit(excluded.optString(i));
        } else if (r.action == ALLOW_ALL) {
            r.types = MAIN_FRAME | SUB_FRAME;
        }
        r.methods = strings(c, "requestMethods");
        r.excludedMethods = strings(c, "excludedRequestMethods");
        r.requestDomains = strings(c, "requestDomains");
        r.excludedRequestDomains = strings(c, "excludedRequestDomains");
        r.initiatorDomains = strings(c, "initiatorDomains");
        if (r.initiatorDomains == null) r.initiatorDomains = strings(c, "domains");
        r.excludedInitiatorDomains = strings(c, "excludedInitiatorDomains");
        if (r.excludedInitiatorDomains == null) r.excludedInitiatorDomains = strings(c, "excludedDomains");
        r.tabIds = ints(c, "tabIds");
        r.excludedTabIds = ints(c, "excludedTabIds");
        String domainType = c.optString("domainType");
        r.domainType = "firstParty".equals(domainType) ? 1 : "thirdParty".equals(domainType) ? 2 : 0;
        return r;
    }

    private static String str(JSONObject o, String key) {
        String s = o.isNull(key) ? "" : o.optString(key, "");
        return s.isEmpty() ? null : s;
    }

    private static Set<String> strings(JSONObject o, String key) {
        JSONArray a = o.optJSONArray(key);
        if (a == null || a.length() == 0) return null;
        Set<String> out = new HashSet<>();
        for (int i = 0; i < a.length(); i++) out.add(a.optString(i).toLowerCase(Locale.ROOT));
        return out;
    }

    private static Set<Integer> ints(JSONObject o, String key) {
        JSONArray a = o.optJSONArray(key);
        if (a == null || a.length() == 0) return null;
        Set<Integer> out = new HashSet<>();
        for (int i = 0; i < a.length(); i++) out.add(a.optInt(i));
        return out;
    }

    static String redirectTarget(Rule r, String url, String extensionBase) {
        if (r.action == UPGRADE) {
            if (url.startsWith("http://")) return "https://" + url.substring(7);
            return url.startsWith("ws://") ? "wss://" + url.substring(5) : null;
        }
        if (r.redirectUrl != null) return r.redirectUrl;
        if (r.extensionPath != null) {
            String p = r.extensionPath;
            while (p.startsWith("/")) p = p.substring(1);
            return extensionBase + p;
        }
        if (r.regexSubstitution != null && r.regexFilter != null) {
            Pattern p = r.pattern();
            if (p == null) return null;
            Matcher m = p.matcher(url);
            if (!m.find()) return null;
            StringBuilder sub = new StringBuilder();
            String s = r.regexSubstitution;
            for (int i = 0; i < s.length(); i++) {
                char c = s.charAt(i);
                if (c == '\\' && i + 1 < s.length()) {
                    char n = s.charAt(++i);
                    if (n >= '0' && n <= '9') {
                        int g = n - '0';
                        if (g <= m.groupCount() && m.group(g) != null) sub.append(m.group(g));
                    } else {
                        sub.append(n);
                    }
                } else {
                    sub.append(c);
                }
            }
            return url.substring(0, m.start()) + sub + url.substring(m.end());
        }
        return r.transform == null ? null : transform(r.transform, url);
    }

    private static String transform(JSONObject t, String url) {
        try {
            URI u = new URI(url);
            String scheme = t.has("scheme") ? t.optString("scheme") : u.getScheme();
            String host = t.has("host") ? t.optString("host") : u.getHost();
            String port = t.has("port") ? t.optString("port") : u.getPort() < 0 ? "" : String.valueOf(u.getPort());
            String path = t.has("path") ? t.optString("path") : u.getRawPath();
            String query = u.getRawQuery() == null ? "" : "?" + u.getRawQuery();
            if (t.has("query")) {
                query = t.optString("query");
            } else if (t.optJSONObject("queryTransform") != null) {
                query = queryTransform(t.optJSONObject("queryTransform"), u.getRawQuery());
            }
            String fragment = u.getRawFragment() == null ? "" : "#" + u.getRawFragment();
            if (t.has("fragment")) fragment = t.optString("fragment");
            if (scheme == null || host == null) return null;
            return scheme + "://" + host + (port.isEmpty() ? "" : ":" + port) + (path == null ? "" : path) + query + fragment;
        } catch (Exception e) {
            return null;
        }
    }

    private static String queryTransform(JSONObject q, String raw) {
        List<String> parts = new ArrayList<>();
        if (raw != null) for (String p : raw.split("&")) if (!p.isEmpty()) parts.add(p);
        Set<String> remove = new HashSet<>();
        JSONArray rm = q.optJSONArray("removeParams");
        if (rm != null) for (int i = 0; i < rm.length(); i++) remove.add(rm.optString(i));
        for (int i = parts.size() - 1; i >= 0; i--) if (remove.contains(key(parts.get(i)))) parts.remove(i);
        JSONArray add = q.optJSONArray("addOrReplaceParams");
        if (add != null) {
            for (int i = 0; i < add.length(); i++) {
                JSONObject kv = add.optJSONObject(i);
                if (kv == null) continue;
                String k = kv.optString("key");
                String pair = k + "=" + kv.optString("value");
                boolean found = false;
                for (int j = 0; j < parts.size(); j++) {
                    if (key(parts.get(j)).equals(k)) {
                        parts.set(j, pair);
                        found = true;
                    }
                }
                if (!found && !kv.optBoolean("replaceOnly", false)) parts.add(pair);
            }
        }
        StringBuilder sb = new StringBuilder();
        for (String p : parts) sb.append(sb.length() == 0 ? '?' : '&').append(p);
        return sb.toString();
    }

    private static String key(String pair) {
        int eq = pair.indexOf('=');
        return eq < 0 ? pair : pair.substring(0, eq);
    }
}
