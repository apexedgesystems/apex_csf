# ==============================================================================
# TemplateApp release manifest
#
# Minimal user-application example: hosted CPU only. Registering here is
# what makes `make release APP=TemplateApp` work; the apex CI release
# matrix (RELEASE_APPS) intentionally excludes user applications.
# ==============================================================================

APP_REGISTRY += TemplateApp

APP_TemplateApp_PLATFORMS          := cpu
APP_TemplateApp_cpu_TYPE           := posix
APP_TemplateApp_cpu_BINARY         := TemplateApp
