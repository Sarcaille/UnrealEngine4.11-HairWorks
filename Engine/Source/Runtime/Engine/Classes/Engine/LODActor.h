// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "LODActor.generated.h"

/**
 * LODActor is an instance of an autogenerated StaticMesh Actors by Hierarchical LOD System
 * This is essentially just StaticMeshActor that you can't move or edit, but it contains multiple actors reference
 *
 * @see https://docs.unrealengine.com/latest/INT/Engine/Actors/LODActor/
 * @see UStaticMesh
 */
UCLASS(notplaceable, hidecategories = (Object, Collision, Display, Input, Blueprint, Transform))
class ENGINE_API ALODActor : public AActor
{
GENERATED_UCLASS_BODY()

private_subobject:
	// disable display of this component
	UPROPERTY(Category=LODActor, VisibleAnywhere)
	class UStaticMeshComponent* StaticMeshComponent;

public:
	UPROPERTY(Category=LODActor, VisibleAnywhere)
	TArray<class AActor*> SubActors;

	/** what distance do you want this to show up instead of SubActors */
	UPROPERTY(Category=LODActor, EditAnywhere, meta=(ClampMin = "0", UIMin = "0"))
	float LODDrawDistance;
	
	/** what distance do you want this to show up instead of SubActors */
	UPROPERTY(Category=LODActor, VisibleAnywhere)
	int32 LODLevel;

	/** assets that were created for this, so that we can delete them */
	UPROPERTY(Category=LODActor, VisibleAnywhere)
	TArray<class UObject*> SubObjects;

#if WITH_EDITOR
	// Begin AActor Interface
	virtual void CheckForErrors() override;
	virtual bool GetReferencedContentObjects( TArray<UObject*>& Objects ) const override;
	// End AActor Interface
#endif // WITH_EDITOR	

	/** Sets StaticMesh and IsPreviewActor to true if InStaticMesh equals nullptr */
	void SetStaticMesh(class UStaticMesh* InStaticMesh);


#if WITH_EDITOR
	/**
	* Adds InAcor to the SubActors array and set its LODParent to this
	* @param InActor - Actor to add
	*/
	void AddSubActor(AActor* InActor);

	/**
	* Removes InActor from the SubActors array and sets its LODParent to nullptr
	* @param InActor - Actor to remove
	*/
	const bool RemoveSubActor(AActor* InActor);

	/**
	* Returns whether or not this LODActor is dirty
	* @return const bool
	*/
	const bool IsDirty() const { return bDirty; }

	/**
	* Sets whether or not this LODActor is dirty and should have its LODMesh (re)build
	* @param bNewState - New dirty state
	*/
	void SetIsDirty(const bool bNewState);
	
	/**
	* Determines whether or not this LODActor has valid SubActors (more than one, or one staticmesh actor)
	* @return const bool
	*/
	const bool HasValidSubActors();

	/** Toggles forcing the StaticMeshComponent drawing distance to 0 or LODDrawDistance */
	void ToggleForceView();

	/** Sets forcing the StaticMeshComponent drawing distance to 0 or LODDrawDistance according to InState*/
	void SetForcedView(const bool InState);

	/** Sets the state of whether or not this LODActor is hidden from the Editor view, used for forcing a HLOD to show*/
	void SetHiddenFromEditorView(const bool InState, const int32 ForceLODLevel);

	/** Returns the number of triangles this LODActor's SubActors contain */
	const uint32 GetNumTrianglesInSubActors();

	/** Returns the number of triangles this LODActor's SubActors contain */
	const uint32 GetNumTrianglesInMergedMesh();
	
	/** Updates the LODParents for the SubActors (and the drawing distance)*/
	void UpdateSubActorLODParents();

	/** Cleans the SubActor array (removes all NULL entries) */
	void CleanSubActorArray();
#endif // WITH_EDITOR
	
	// Begin UObject interface.
	virtual FString GetDetailedInfoInternal() const override;
	virtual FBox GetComponentsBoundingBox(bool bNonColliding = false) const override;	
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void EditorApplyTranslation(const FVector& DeltaTranslation, bool bAltDown, bool bShiftDown, bool bCtrlDown) override;
	virtual void EditorApplyRotation(const FRotator& DeltaRotation, bool bAltDown, bool bShiftDown, bool bCtrlDown) override;
	virtual void EditorApplyScale(const FVector& DeltaScale, const FVector* PivotLocation, bool bAltDown, bool bShiftDown, bool bCtrlDown) override;
	virtual void EditorApplyMirror(const FVector& MirrorScale, const FVector& PivotLocation) override;
#endif // WITH_EDITOR	
	virtual void PostRegisterAllComponents() override;
	// End UObject interface.		
protected:
#if WITH_EDITORONLY_DATA
	/** Whether or not this LODActor is not build or needs rebuilding */
	UPROPERTY()
	bool bDirty;
#endif // WITH_EDITORONLY_DATA
public:
#if WITH_EDITORONLY_DATA
	/** Cached number of triangles contained in the SubActors*/
	UPROPERTY()
	uint32 NumTrianglesInSubActors;

	/** Cached number of triangles contained in the SubActors*/
	UPROPERTY()
	uint32 NumTrianglesInMergedMesh;
#endif // WITH_EDITORONLY_DATA

	/** Returns StaticMeshComponent subobject **/
	class UStaticMeshComponent* GetStaticMeshComponent() const { return StaticMeshComponent; }
}; 