#pragma once

struct Node;
class App;

// Parameter Edit Dialog
void OpenParameterEditDialog(Node* node, App* app);
void RenderParameterEditDialog();
bool IsParameterEditDialogOpen();

