{
	global:
		ngx_modules;
		ngx_module_names;
		ngx_module_order;
		datadog_version_*;
    datadog_semver_*;
	local: *; /* Hide all other symbols */
};
