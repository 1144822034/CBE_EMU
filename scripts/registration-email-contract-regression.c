#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    char email[VM_MOCK_REGISTRATION_EMAIL_MAX];
    char encoded[64];
    char captchaToken[VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN + 1];
    char normalizedCaptchaToken[VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN + 1];
    char captchaCode[VM_MOCK_REGISTRATION_CAPTCHA_CODE_LEN + 1];
    char normalizedCaptchaCode[VM_MOCK_REGISTRATION_CAPTCHA_CODE_LEN + 1];
    char renderedBody[VM_MOCK_REGISTRATION_EMAIL_BODY_MAX];
    char escapedBody[VM_MOCK_REGISTRATION_SMTP_MESSAGE_MAX];
    const u8 auth[] = {0, 'u', 's', 'e', 'r', 0, 's', 'e', 'c', 'r', 'e', 't'};
    vm_mock_registration_smtp_config smtp;
    vm_mock_registration_email_template emailTemplate;

    if (!vm_mock_registration_normalize_email("Player.Name+1@Example.COM",
                                              email, sizeof(email)) ||
        strcmp(email, "player.name+1@example.com") != 0 ||
        vm_mock_registration_normalize_email("missing-at.example.com", email,
                                             sizeof(email)) ||
        vm_mock_registration_normalize_email("two@@example.com", email,
                                             sizeof(email)) ||
        !vm_mock_registration_code_is_valid("012345") ||
        vm_mock_registration_code_is_valid("12345a") ||
        vm_mock_registration_base64_encode(auth, sizeof(auth), encoded,
                                           sizeof(encoded)) == 0 ||
        strcmp(encoded, "AHVzZXIAc2VjcmV0") != 0)
    {
        fprintf(stderr, "registration email input contract regressed\n");
        return 1;
    }
    if (!vm_mock_registration_generate_captcha(captchaToken, captchaCode) ||
        strlen(captchaToken) != VM_MOCK_REGISTRATION_CAPTCHA_TOKEN_LEN ||
        !vm_mock_registration_captcha_token_normalize(captchaToken,
                                                       normalizedCaptchaToken) ||
        !vm_mock_registration_captcha_code_normalize("a2b3c",
                                                      normalizedCaptchaCode) ||
        strcmp(normalizedCaptchaCode, "A2B3C") != 0 ||
        vm_mock_registration_captcha_code_normalize("i2b3c",
                                                     normalizedCaptchaCode))
    {
        fprintf(stderr, "registration image captcha contract regressed\n");
        return 1;
    }
    if (strcmp(VM_MOCK_REGISTRATION_SQL_TIMESTAMP_NOW,
               "CURRENT_TIMESTAMP()") != 0)
    {
        fprintf(stderr, "registration verification timestamp contract regressed\n");
        return 1;
    }
    memset(&emailTemplate, 0, sizeof(emailTemplate));
    snprintf(emailTemplate.subject, sizeof(emailTemplate.subject),
             "江湖OL 注册验证码");
    snprintf(emailTemplate.body, sizeof(emailTemplate.body),
             "您的验证码是 {{code}}\n.\n请勿泄露。\n");
    if (!vm_mock_registration_email_template_ready(&emailTemplate) ||
        !vm_mock_registration_render_email_body(&emailTemplate, "012345",
                                                renderedBody,
                                                sizeof(renderedBody)) ||
        strstr(renderedBody, "{{code}}") != NULL ||
        strstr(renderedBody, "012345") == NULL ||
        !vm_mock_registration_smtp_escape_body(renderedBody, escapedBody,
                                               sizeof(escapedBody)) ||
        strstr(escapedBody, "\r\n..\r\n") == NULL ||
        vm_mock_registration_email_subject_valid("bad\r\nsubject"))
    {
        fprintf(stderr, "registration email template contract regressed\n");
        return 1;
    }
    snprintf(emailTemplate.body, sizeof(emailTemplate.body), "缺少验证码占位符");
    if (vm_mock_registration_email_template_ready(&emailTemplate))
    {
        fprintf(stderr, "registration email placeholder contract regressed\n");
        return 1;
    }
    memset(&smtp, 0, sizeof(smtp));
    smtp.enabled = true;
    smtp.port = 25;
    snprintf(smtp.host, sizeof(smtp.host), "127.0.0.1");
    snprintf(smtp.senderEmail, sizeof(smtp.senderEmail), "noreply@example.com");
    if (!vm_mock_registration_smtp_config_ready(&smtp))
    {
        fprintf(stderr, "minimal relay SMTP configuration was rejected\n");
        return 1;
    }
    smtp.username[0] = 'u';
    if (vm_mock_registration_smtp_config_ready(&smtp) ||
        strstr(g_vm_mock_admin_script, "tab=registration") == NULL ||
        strstr(g_vm_mock_admin_script, "注册设置") == NULL)
    {
        fprintf(stderr, "registration policy or SMTP navigation contract regressed\n");
        return 1;
    }
    vm_mock_admin_operation_audit_begin("admin", "save-registration-settings",
                                        "");
    if (!g_vm_mock_admin_operation_audit_context.active ||
        strcmp(g_vm_mock_admin_operation_audit_context.targetAccountId,
               VM_MOCK_ADMIN_OPERATION_LOG_CONFIG_TARGET) != 0)
    {
        fprintf(stderr, "registration settings audit target regressed\n");
        return 1;
    }
    vm_mock_admin_operation_audit_clear();
    if (strstr(g_vm_mock_registration_browser_script,
               "/user/register/captcha/new") == NULL ||
        strstr(g_vm_mock_registration_browser_script, "payload.svg") == NULL ||
        strstr(g_vm_mock_registration_browser_script,
               "addEventListener('click',open)") == NULL ||
        strstr(g_vm_mock_registration_browser_script, "onclick") != NULL)
    {
        fprintf(stderr, "registration captcha CSP contract regressed\n");
        return 1;
    }
    puts("registration email contract regression passed");
    return 0;
}
