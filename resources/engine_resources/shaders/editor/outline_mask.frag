#version 430 core

out vec4 fragColor;

// EditorOutlineMaskPass only ever draws the currently selected actor's own model(s) - see
// EditorMainWindow::SetSelectedActor and Model::AddToPass/RemoveFromPass - so every fragment reaching
// this shader already belongs to the selected object; no objID comparison needed.
void main(){
    fragColor = vec4(1,1,1,1);
}
