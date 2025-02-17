package com.matter.tv.app.api;

public class MatterIntentConstants {

  public static final String ACTION_MATTER_COMMAND = "com.matter.tv.app.api.action.MATTER_COMMAND";

  public static final String ACTION_MATTER_AGENT = "com.matter.tv.app.api.action.MatterAppAgent";

  public static final String PERMISSION_MATTER_AGENT_BIND =
      "com.matter.tv.app.api.permission.BIND_SERVICE_PERMISSION";

  public static final String PERMISSION_MATTER_AGENT = "com.matter.tv.app.api.permission.SEND_DATA";

  public static final String EXTRA_COMMAND_PAYLOAD = "EXTRA_COMMAND_PAYLOAD";

  public static final String EXTRA_RESPONSE_PAYLOAD = "EXTRA_RESPONSE_PAYLOAD";

  public static final String EXTRA_DIRECTIVE_RESPONSE_PENDING_INTENT =
      "EXTRA_DIRECTIVE_RESPONSE_PENDING_INTENT";
}
