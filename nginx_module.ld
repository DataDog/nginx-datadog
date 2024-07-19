{
	global:
		ngx_modules;
		ngx_module_names;
		ngx_module_order;
		datadog_version_*;
		ddatadog_semver_*;
		ddatadog_build_id_nginx_mod;
	local: *; /* Hide all other symbols */
};
